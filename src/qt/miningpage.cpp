// Copyright (c) 2026 The Offerings Conclave / SubGenius.Finance community
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miningpage.h"
#include "ui_miningpage.h"

#include "clientmodel.h"
#include "rpcserver.h"
#include "stratum.h"

#include <QSettings>
#include <QThread>
#include <QTimer>

// dHashesPerSec is the same global the gethashespersec / getmininginfo RPCs
// read; defined in src/miner.cpp.
extern double dHashesPerSec;

// OFFSIG window — solo mining is locked in this height range because the chain
// only accepts Conclave-signed blocks. Outside it solo mining is permissionless.
// Mirrors chainparams.cpp::nSignedWindowStart / nOpenMiningHeight.
static const int kOffsigWindowStart = 999991;
static const int kOffsigWindowEnd   = 1050666;
static const int kMiningPollMs      = 2000;

MiningPage::MiningPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MiningPage),
    clientModel(0),
    miningPollTimer(0),
    miningSuspendedByOffsig(false)
{
    ui->setupUi(this);

    int idealThreads = QThread::idealThreadCount();
    if (idealThreads < 1) idealThreads = 1;
    ui->miningThreads->setMaximum(idealThreads);

    miningPollTimer = new QTimer(this);
    miningPollTimer->setInterval(kMiningPollMs);
    connect(miningPollTimer, SIGNAL(timeout()), this, SLOT(updateMiningStatus()));
    connect(miningPollTimer, SIGNAL(timeout()), this, SLOT(updatePoolMiningStatus()));

    // Reflect a solo miner already started with -gen or from the console.
    try
    {
        json_spirit::Value gen = tableRPC.execute("getgenerate", json_spirit::Array());
        if (gen.type() == json_spirit::bool_type && gen.get_bool())
        {
            ui->miningEnable->blockSignals(true);
            ui->miningEnable->setChecked(true);
            ui->miningEnable->blockSignals(false);
        }
    }
    catch (...) {}

    // Pool mode: restore last-used settings; reflect a client already started
    // via -stratum/-stratumuser command-line args or the setstratum RPC.
    QSettings settings;
    ui->poolMiningEndpoint->setText(
        settings.value("poolMiningEndpoint", "pool.23skidoo.info:3040").toString());
    ui->poolMiningAddress->setText(settings.value("poolMiningAddress", "").toString());
    // The only way this box arrives filled is a previous session on this
    // computer: no address ships with the wallet. Say so, so nobody takes
    // it for a preset that pays someone else.
    ui->poolMiningAddressNote->setVisible(!ui->poolMiningAddress->text().isEmpty());
    int nSavedThreads = settings.value("poolMiningThreads", 1).toInt();
    ui->poolMiningThreads->setMaximum(idealThreads);
    ui->poolMiningThreads->setValue(qBound(1, nSavedThreads, idealThreads));
    if (g_pStratumClient && g_pStratumClient->IsRunning())
    {
        ui->poolMiningToggle->setChecked(true);
        ui->poolMiningToggle->setText(tr("Stop Pool Mining"));
        ui->poolMiningEndpoint->setEnabled(false);
        ui->poolMiningAddress->setEnabled(false);
        ui->poolMiningThreads->setEnabled(false);
    }

    updateMiningStatus();
    updatePoolMiningStatus();
}

MiningPage::~MiningPage()
{
    delete ui;
}

void MiningPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if (model)
    {
        connect(model, SIGNAL(numBlocksChanged(int)), this, SLOT(updateMiningOffsigGuard(int)));
        updateMiningOffsigGuard(model->getNumBlocks());
    }
}

// Only poll the hashrate while the page is on screen.
void MiningPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateMiningStatus();
    updatePoolMiningStatus();
    miningPollTimer->start();
}

void MiningPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    miningPollTimer->stop();
}

bool MiningPage::setGenerate(bool fGenerate, int nThreads)
{
    json_spirit::Array params;
    params.push_back(fGenerate);
    if (fGenerate)
        params.push_back(nThreads);
    try
    {
        tableRPC.execute("setgenerate", params);
        return true;
    }
    catch (json_spirit::Object& objError)
    {
        json_spirit::Value msg = json_spirit::find_value(objError, "message");
        ui->miningStatus->setText(msg.type() == json_spirit::str_type
            ? QString::fromStdString(msg.get_str())
            : tr("The miner refused the offering."));
    }
    catch (std::exception& e)
    {
        ui->miningStatus->setText(QString::fromStdString(e.what()));
    }
    return false;
}

// ============================================================================
// Solo mode
// ============================================================================

void MiningPage::on_miningEnable_toggled(bool checked)
{
    // OFFSIG window guard. The checkbox is greyed inside the window, but be
    // defensive and bounce a toggle-on straight back off.
    if (checked && clientModel)
    {
        int h = clientModel->getNumBlocks();
        if (h >= kOffsigWindowStart && h <= kOffsigWindowEnd)
        {
            ui->miningEnable->blockSignals(true);
            ui->miningEnable->setChecked(false);
            ui->miningEnable->blockSignals(false);
            ui->miningStatus->setText(tr("Mining locked during the Codex window."));
            return;
        }
    }

    if (!setGenerate(checked, ui->miningThreads->value()))
    {
        ui->miningEnable->blockSignals(true);
        ui->miningEnable->setChecked(!checked);
        ui->miningEnable->blockSignals(false);
        return;
    }
    updateMiningStatus();
}

void MiningPage::on_miningThreads_valueChanged(int value)
{
    // While mining, restart with the new thread count. Otherwise the value
    // just sits in the spinner and takes effect on the next toggle-on.
    if (ui->miningEnable->isChecked())
        setGenerate(true, value);
}

void MiningPage::updateMiningStatus()
{
    // OFFSIG banner takes precedence — keep that line until the window passes.
    if (miningSuspendedByOffsig)
        return;

    if (!ui->miningEnable->isChecked())
    {
        ui->miningStatus->setText(tr("Mining stopped."));
        return;
    }

    double hps = dHashesPerSec;
    if (hps <= 0.0)
        ui->miningStatus->setText(tr("Mining starting…"));
    else if (hps < 1000.0)
        ui->miningStatus->setText(tr("Mining at %1 H/s").arg(hps, 0, 'f', 1));
    else if (hps < 1e6)
        ui->miningStatus->setText(tr("Mining at %1 kH/s").arg(hps / 1e3, 0, 'f', 2));
    else
        ui->miningStatus->setText(tr("Mining at %1 MH/s").arg(hps / 1e6, 0, 'f', 2));
}

void MiningPage::updateMiningOffsigGuard(int height)
{
    bool inWindow = (height >= kOffsigWindowStart && height <= kOffsigWindowEnd);

    ui->miningOffsigBanner->setVisible(inWindow);
    ui->miningEnable->setEnabled(!inWindow);
    ui->miningThreads->setEnabled(!inWindow);

    if (inWindow)
    {
        // If solo mining was running when we entered the window, stop it. We
        // don't auto-restore when the window ends — the user re-enables.
        if (ui->miningEnable->isChecked())
        {
            ui->miningEnable->blockSignals(true);
            ui->miningEnable->setChecked(false);
            ui->miningEnable->blockSignals(false);
            setGenerate(false, 0);
        }
        miningSuspendedByOffsig = true;
        ui->miningStatus->setText(tr("Mining locked — Codex window in effect."));
    }
    else if (miningSuspendedByOffsig)
    {
        miningSuspendedByOffsig = false;
        updateMiningStatus();
    }
}

// ============================================================================
// Pool mode — The Communal Rite (in-wallet stratum client).
// Deliberately NOT gated by the OFFSIG guard: pool blocks carry the
// Conclave signature.
// ============================================================================

void MiningPage::on_poolMiningToggle_clicked(bool checked)
{
    if (!checked)
    {
        StopStratum();
        ui->poolMiningToggle->setText(tr("Start Pool Mining"));
        ui->poolMiningEndpoint->setEnabled(true);
        ui->poolMiningAddress->setEnabled(true);
        ui->poolMiningThreads->setEnabled(true);
        updatePoolMiningStatus();
        return;
    }

    // Validate inputs before spinning anything up.
    QString strEndpoint = ui->poolMiningEndpoint->text().trimmed();
    QString strAddress = ui->poolMiningAddress->text().trimmed();
    int nColon = strEndpoint.lastIndexOf(':');
    QString strHost = (nColon > 0) ? strEndpoint.left(nColon) : QString();
    int nPort = (nColon > 0) ? strEndpoint.mid(nColon + 1).toInt() : 0;

    QString strProblem;
    if (strHost.isEmpty() || nPort <= 0 || nPort > 65535)
        strProblem = tr("Enter the pool as host:port.");
    else if (strAddress.isEmpty() || !strAddress.startsWith("Q") || strAddress.length() < 26)
        strProblem = tr("Enter a valid OFF pay-to address (starts with Q).");

    if (!strProblem.isEmpty())
    {
        ui->poolMiningToggle->blockSignals(true);
        ui->poolMiningToggle->setChecked(false);
        ui->poolMiningToggle->blockSignals(false);
        ui->poolMiningStatus->setText(strProblem);
        return;
    }

    // Replace any prior client (also covers one left over from -stratum
    // command-line args or the setstratum RPC) with one built from the form.
    if (!StartStratum(strHost.toStdString(), nPort, strAddress.toStdString(),
                      ui->poolMiningThreads->value()))
    {
        ui->poolMiningToggle->blockSignals(true);
        ui->poolMiningToggle->setChecked(false);
        ui->poolMiningToggle->blockSignals(false);
        ui->poolMiningStatus->setText(tr("Failed to start the pool client."));
        return;
    }

    QSettings settings;
    settings.setValue("poolMiningEndpoint", strEndpoint);
    settings.setValue("poolMiningAddress", strAddress);
    settings.setValue("poolMiningThreads", ui->poolMiningThreads->value());

    ui->poolMiningToggle->setText(tr("Stop Pool Mining"));
    ui->poolMiningEndpoint->setEnabled(false);
    ui->poolMiningAddress->setEnabled(false);
    ui->poolMiningThreads->setEnabled(false);
    ui->poolMiningStatus->setText(tr("Connecting to %1…").arg(strEndpoint));
}

void MiningPage::updatePoolMiningStatus()
{
    if (!g_pStratumClient || !g_pStratumClient->IsRunning())
    {
        if (!ui->poolMiningToggle->isChecked())
            ui->poolMiningStatus->setText(tr("Pool mining stopped."));
        return;
    }

    if (!g_pStratumClient->IsConnected())
    {
        QString strErr = QString::fromStdString(g_pStratumClient->GetLastError());
        ui->poolMiningStatus->setText(strErr.isEmpty()
            ? tr("Connecting…")
            : tr("Reconnecting… (%1)").arg(strErr));
        return;
    }
    if (!g_pStratumClient->IsAuthorized())
    {
        ui->poolMiningStatus->setText(tr("Connected — authorizing…"));
        return;
    }

    double dRate = g_pStratumClient->GetHashRate();
    QString strRate;
    if (dRate < 1000.0)
        strRate = tr("%1 H/s").arg(dRate, 0, 'f', 1);
    else if (dRate < 1e6)
        strRate = tr("%1 kH/s").arg(dRate / 1e3, 0, 'f', 2);
    else
        strRate = tr("%1 MH/s").arg(dRate / 1e6, 0, 'f', 2);

    ui->poolMiningStatus->setText(tr("Offering at %1 · share diff %2 · accepted %3 / rejected %4")
        .arg(strRate)
        .arg(g_pStratumClient->GetDifficulty())
        .arg(g_pStratumClient->GetSharesAccepted())
        .arg(g_pStratumClient->GetSharesRejected()));
}

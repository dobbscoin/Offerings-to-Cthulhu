// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2013-2014 The Offerings developers
// Copyright (c) 2026 The Offerings Conclave / SubGenius.Finance community
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcconsole.h"
#include "ui_rpcconsole.h"

#include "clientmodel.h"
#include "guiutil.h"
#include "peertablemodel.h"

#include "rpcserver.h"
#include "rpcclient.h"
#include "stratum.h"

#include "util.h"

#include "json/json_spirit_value.h"
#include <openssl/crypto.h>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMenu>
#include <QScrollBar>
#include <QSettings>
#include <QThread>
#include <QTime>
#include <QTimer>

#if QT_VERSION < 0x050000
#include <QUrl>
#endif

// Issue #8 PR A — Mining tab live status. dHashesPerSec is the same global the
// gethashespersec / getmininginfo RPCs read; defined in src/miner.cpp.
extern double dHashesPerSec;

// OFFSIG window — solo mining is locked in this height range because the chain
// only accepts Conclave-signed blocks. Outside it solo mining is permissionless.
// Mirrors chainparams.cpp::nSignedWindowStart / nOpenMiningHeight.
static const int kOffsigWindowStart = 999991;
static const int kOffsigWindowEnd   = 1050666;
static const int kMiningPollMs      = 2000;

// TODO: add a scrollback limit, as there is currently none
// TODO: make it possible to filter out categories (esp debug messages when implemented)
// TODO: receive errors and debug messages through ClientModel

const int CONSOLE_HISTORY = 50;
const QSize ICON_SIZE(24, 24);

const int INITIAL_TRAFFIC_GRAPH_MINS = 30;

const struct {
    const char *url;
    const char *source;
} ICON_MAPPING[] = {
    {"cmd-request", ":/icons/tx_input"},
    {"cmd-reply", ":/icons/tx_output"},
    {"cmd-error", ":/icons/tx_output"},
    {"misc", ":/icons/tx_inout"},
    {NULL, NULL}
};

/* Object for executing console RPC commands in a separate thread.
*/
class RPCExecutor : public QObject
{
    Q_OBJECT

public slots:
    void request(const QString &command);

signals:
    void reply(int category, const QString &command);
};

#include "rpcconsole.moc"

/**
 * Split shell command line into a list of arguments. Aims to emulate \c bash and friends.
 *
 * - Arguments are delimited with whitespace
 * - Extra whitespace at the beginning and end and between arguments will be ignored
 * - Text can be "double" or 'single' quoted
 * - The backslash \c \ is used as escape character
 *   - Outside quotes, any character can be escaped
 *   - Within double quotes, only escape \c " and backslashes before a \c " or another backslash
 *   - Within single quotes, no escaping is possible and no special interpretation takes place
 *
 * @param[out]   args        Parsed arguments will be appended to this list
 * @param[in]    strCommand  Command line to split
 */
bool parseCommandLine(std::vector<std::string> &args, const std::string &strCommand)
{
    enum CmdParseState
    {
        STATE_EATING_SPACES,
        STATE_ARGUMENT,
        STATE_SINGLEQUOTED,
        STATE_DOUBLEQUOTED,
        STATE_ESCAPE_OUTER,
        STATE_ESCAPE_DOUBLEQUOTED
    } state = STATE_EATING_SPACES;
    std::string curarg;
    foreach(char ch, strCommand)
    {
        switch(state)
        {
        case STATE_ARGUMENT: // In or after argument
        case STATE_EATING_SPACES: // Handle runs of whitespace
            switch(ch)
            {
            case '"': state = STATE_DOUBLEQUOTED; break;
            case '\'': state = STATE_SINGLEQUOTED; break;
            case '\\': state = STATE_ESCAPE_OUTER; break;
            case ' ': case '\n': case '\t':
                if(state == STATE_ARGUMENT) // Space ends argument
                {
                    args.push_back(curarg);
                    curarg.clear();
                }
                state = STATE_EATING_SPACES;
                break;
            default: curarg += ch; state = STATE_ARGUMENT;
            }
            break;
        case STATE_SINGLEQUOTED: // Single-quoted string
            switch(ch)
            {
            case '\'': state = STATE_ARGUMENT; break;
            default: curarg += ch;
            }
            break;
        case STATE_DOUBLEQUOTED: // Double-quoted string
            switch(ch)
            {
            case '"': state = STATE_ARGUMENT; break;
            case '\\': state = STATE_ESCAPE_DOUBLEQUOTED; break;
            default: curarg += ch;
            }
            break;
        case STATE_ESCAPE_OUTER: // '\' outside quotes
            curarg += ch; state = STATE_ARGUMENT;
            break;
        case STATE_ESCAPE_DOUBLEQUOTED: // '\' in double-quoted text
            if(ch != '"' && ch != '\\') curarg += '\\'; // keep '\' for everything but the quote and '\' itself
            curarg += ch; state = STATE_DOUBLEQUOTED;
            break;
        }
    }
    switch(state) // final state
    {
    case STATE_EATING_SPACES:
        return true;
    case STATE_ARGUMENT:
        args.push_back(curarg);
        return true;
    default: // ERROR to end in one of the other states
        return false;
    }
}

void RPCExecutor::request(const QString &command)
{
    std::vector<std::string> args;
    if(!parseCommandLine(args, command.toStdString()))
    {
        emit reply(RPCConsole::CMD_ERROR, QString("Parse error: unbalanced ' or \""));
        return;
    }
    if(args.empty())
        return; // Nothing to do
    try
    {
        std::string strPrint;
        // Convert argument list to JSON objects in method-dependent way,
        // and pass it along with the method name to the dispatcher.
        json_spirit::Value result = tableRPC.execute(
            args[0],
            RPCConvertValues(args[0], std::vector<std::string>(args.begin() + 1, args.end())));

        // Format result reply
        if (result.type() == json_spirit::null_type)
            strPrint = "";
        else if (result.type() == json_spirit::str_type)
            strPrint = result.get_str();
        else
            strPrint = write_string(result, true);

        emit reply(RPCConsole::CMD_REPLY, QString::fromStdString(strPrint));
    }
    catch (json_spirit::Object& objError)
    {
        try // Nice formatting for standard-format error
        {
            int code = find_value(objError, "code").get_int();
            std::string message = find_value(objError, "message").get_str();
            emit reply(RPCConsole::CMD_ERROR, QString::fromStdString(message) + " (code " + QString::number(code) + ")");
        }
        catch(std::runtime_error &) // raised when converting to invalid type, i.e. missing code or message
        {   // Show raw JSON object
            emit reply(RPCConsole::CMD_ERROR, QString::fromStdString(write_string(json_spirit::Value(objError), false)));
        }
    }
    catch (std::exception& e)
    {
        emit reply(RPCConsole::CMD_ERROR, QString("Error: ") + QString::fromStdString(e.what()));
    }
}

RPCConsole::RPCConsole(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::RPCConsole),
    clientModel(0),
    historyPtr(0),
    cachedNodeid(-1),
    peersTableContextMenu(0),
    miningPollTimer(0),
    miningSuspendedByOffsig(false)
{
    ui->setupUi(this);
    GUIUtil::restoreWindowGeometry("nRPCConsoleWindow", this->size(), this);

#ifndef Q_OS_MAC
    ui->openDebugLogfileButton->setIcon(QIcon(":/icons/export"));
#endif

    // Install event filter for up and down arrow
    ui->lineEdit->installEventFilter(this);
    ui->messagesWidget->installEventFilter(this);

    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));
    connect(ui->btnClearTrafficGraph, SIGNAL(clicked()), ui->trafficGraph, SLOT(clear()));

    // set OpenSSL version label
    ui->openSSLVersion->setText(SSLeay_version(SSLEAY_VERSION));

    // Mining tab (issue #8 PR A — solo mode only).
    // Spinner max = idealThreadCount. Timer polls dHashesPerSec while the tab is visible.
    int idealThreads = QThread::idealThreadCount();
    if (idealThreads < 1) idealThreads = 1;
    ui->miningThreads->setMaximum(idealThreads);
    miningPollTimer = new QTimer(this);
    miningPollTimer->setInterval(kMiningPollMs);
    connect(miningPollTimer, SIGNAL(timeout()), this, SLOT(updateMiningStatus()));
    connect(miningPollTimer, SIGNAL(timeout()), this, SLOT(updatePoolMiningStatus()));

    // Pool mode (issue #8 phase 3): restore last-used settings; reflect a
    // client already started via -stratum/-stratumuser command-line args.
    {
        QSettings settings;
        ui->poolMiningEndpoint->setText(
            settings.value("poolMiningEndpoint", "pool.23skidoo.info:3040").toString());
        ui->poolMiningAddress->setText(settings.value("poolMiningAddress", "").toString());
        int nSavedThreads = settings.value("poolMiningThreads", 1).toInt();
        ui->poolMiningThreads->setMaximum(idealThreads);
        ui->poolMiningThreads->setValue(qBound(1, nSavedThreads, idealThreads));
        if (g_pStratumClient && g_pStratumClient->IsRunning())
        {
            ui->poolMiningToggle->setChecked(true);
            ui->poolMiningToggle->setText(tr("Stop Pool Mining"));
            updatePoolMiningStatus();
        }
    }

    startExecutor();
    setTrafficGraphRange(INITIAL_TRAFFIC_GRAPH_MINS);

    clear();
}

RPCConsole::~RPCConsole()
{
    GUIUtil::saveWindowGeometry("nRPCConsoleWindow", this);
    emit stopExecutor();
    delete ui;
}

bool RPCConsole::eventFilter(QObject* obj, QEvent *event)
{
    if(event->type() == QEvent::KeyPress) // Special key handling
    {
        QKeyEvent *keyevt = static_cast<QKeyEvent*>(event);
        int key = keyevt->key();
        Qt::KeyboardModifiers mod = keyevt->modifiers();
        switch(key)
        {
        case Qt::Key_Up: if(obj == ui->lineEdit) { browseHistory(-1); return true; } break;
        case Qt::Key_Down: if(obj == ui->lineEdit) { browseHistory(1); return true; } break;
        case Qt::Key_PageUp: /* pass paging keys to messages widget */
        case Qt::Key_PageDown:
            if(obj == ui->lineEdit)
            {
                QApplication::postEvent(ui->messagesWidget, new QKeyEvent(*keyevt));
                return true;
            }
            break;
        default:
            // Typing in messages widget brings focus to line edit, and redirects key there
            // Exclude most combinations and keys that emit no text, except paste shortcuts
            if(obj == ui->messagesWidget && (
                  (!mod && !keyevt->text().isEmpty() && key != Qt::Key_Tab) ||
                  ((mod & Qt::ControlModifier) && key == Qt::Key_V) ||
                  ((mod & Qt::ShiftModifier) && key == Qt::Key_Insert)))
            {
                ui->lineEdit->setFocus();
                QApplication::postEvent(ui->lineEdit, new QKeyEvent(*keyevt));
                return true;
            }
        }
    }
    return QDialog::eventFilter(obj, event);
}

void RPCConsole::setClientModel(ClientModel *model)
{
    clientModel = model;
    ui->trafficGraph->setClientModel(model);
    if(model)
    {
        // Keep up to date with client
        setNumConnections(model->getNumConnections());
        connect(model, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));

        setNumBlocks(model->getNumBlocks());
        connect(model, SIGNAL(numBlocksChanged(int)), this, SLOT(setNumBlocks(int)));

        updateTrafficStats(model->getTotalBytesRecv(), model->getTotalBytesSent());
        connect(model, SIGNAL(bytesChanged(quint64,quint64)), this, SLOT(updateTrafficStats(quint64, quint64)));

        // set up peer table
        ui->peerWidget->setModel(model->getPeerTableModel());
        ui->peerWidget->verticalHeader()->hide();
        ui->peerWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui->peerWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->peerWidget->setSelectionMode(QAbstractItemView::SingleSelection);
        ui->peerWidget->setColumnWidth(PeerTableModel::NetNodeId, NETNODEID_COLUMN_WIDTH);
        ui->peerWidget->setColumnWidth(PeerTableModel::Address, ADDRESS_COLUMN_WIDTH);
        ui->peerWidget->setColumnWidth(PeerTableModel::Subversion, SUBVERSION_COLUMN_WIDTH);
        ui->peerWidget->setColumnWidth(PeerTableModel::Ping, PING_COLUMN_WIDTH);

        // connect the peerWidget selection model to our peerSelected() handler
        connect(ui->peerWidget->selectionModel(), SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)),
             this, SLOT(peerSelected(const QItemSelection &, const QItemSelection &)));
        connect(model->getPeerTableModel(), SIGNAL(layoutChanged()), this, SLOT(peerLayoutChanged()));

        // peers table right-click context menu — Copy Address, Disconnect Node
        peersTableContextMenu = new QMenu(this);
        peersTableContextMenu->addAction(tr("&Copy address"), this, SLOT(copyPeerAddress()));
        peersTableContextMenu->addSeparator();
        peersTableContextMenu->addAction(tr("&Disconnect Node"), this, SLOT(disconnectSelectedPeer()));
        ui->peerWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(ui->peerWidget, SIGNAL(customContextMenuRequested(const QPoint&)),
                this, SLOT(showPeersTableContextMenu(const QPoint&)));

        // Provide initial values
        ui->clientVersion->setText(model->formatFullVersion());
        ui->clientName->setText(model->clientName());
        ui->buildDate->setText(model->formatBuildDate());
        ui->startupTime->setText(model->formatClientStartupTime());

        ui->networkName->setText(model->getNetworkName());
    }
}

static QString categoryClass(int category)
{
    switch(category)
    {
    case RPCConsole::CMD_REQUEST:  return "cmd-request"; break;
    case RPCConsole::CMD_REPLY:    return "cmd-reply"; break;
    case RPCConsole::CMD_ERROR:    return "cmd-error"; break;
    default:                       return "misc";
    }
}

void RPCConsole::clear()
{
    ui->messagesWidget->clear();
    history.clear();
    historyPtr = 0;
    ui->lineEdit->clear();
    ui->lineEdit->setFocus();

    // Add smoothly scaled icon images.
    // (when using width/height on an img, Qt uses nearest instead of linear interpolation)
    for(int i=0; ICON_MAPPING[i].url; ++i)
    {
        ui->messagesWidget->document()->addResource(
                    QTextDocument::ImageResource,
                    QUrl(ICON_MAPPING[i].url),
                    QImage(ICON_MAPPING[i].source).scaled(ICON_SIZE, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }

    // Set default style sheet
    ui->messagesWidget->document()->setDefaultStyleSheet(
                "table { }"
                "td.time { color: #808080; padding-top: 3px; } "
                "td.message { font-family: monospace; font-size: 12px; } " // Todo: Remove fixed font-size
                "td.cmd-request { color: #006060; } "
                "td.cmd-error { color: red; } "
                "b { color: #006060; } "
                );

    message(CMD_REPLY, (tr("Welcome to the Offerings RPC console.") + "<br>" +
                        tr("Use up and down arrows to navigate history, and <b>Ctrl-L</b> to clear screen.") + "<br>" +
                        tr("Type <b>help</b> for an overview of available commands.")), true);
}

void RPCConsole::reject()
{
    // Ignore escape keypress if this is not a seperate window
    if(windowType() != Qt::Widget)
        QDialog::reject();
}

void RPCConsole::message(int category, const QString &message, bool html)
{
    QTime time = QTime::currentTime();
    QString timeString = time.toString();
    QString out;
    out += "<table><tr><td class=\"time\" width=\"65\">" + timeString + "</td>";
    out += "<td class=\"icon\" width=\"32\"><img src=\"" + categoryClass(category) + "\"></td>";
    out += "<td class=\"message " + categoryClass(category) + "\" valign=\"middle\">";
    if(html)
        out += message;
    else
        out += GUIUtil::HtmlEscape(message, true);
    out += "</td></tr></table>";
    ui->messagesWidget->append(out);
}

void RPCConsole::setNumConnections(int count)
{
    if (!clientModel)
        return;

    QString connections = QString::number(count) + " (";
    connections += tr("In:") + " " + QString::number(clientModel->getNumConnections(CONNECTIONS_IN)) + " / ";
    connections += tr("Out:") + " " + QString::number(clientModel->getNumConnections(CONNECTIONS_OUT)) + ")";

    ui->numberOfConnections->setText(connections);
}

void RPCConsole::setNumBlocks(int count)
{
    ui->numberOfBlocks->setText(QString::number(count));
    if(clientModel)
        ui->lastBlockTime->setText(clientModel->getLastBlockDate().toString());
    updateMiningOffsigGuard(count);
}

void RPCConsole::on_lineEdit_returnPressed()
{
    QString cmd = ui->lineEdit->text();
    ui->lineEdit->clear();

    if(!cmd.isEmpty())
    {
        message(CMD_REQUEST, cmd);
        emit cmdRequest(cmd);
        // Truncate history from current position
        history.erase(history.begin() + historyPtr, history.end());
        // Append command to history
        history.append(cmd);
        // Enforce maximum history size
        while(history.size() > CONSOLE_HISTORY)
            history.removeFirst();
        // Set pointer to end of history
        historyPtr = history.size();
        // Scroll console view to end
        scrollToEnd();
    }
}

void RPCConsole::browseHistory(int offset)
{
    historyPtr += offset;
    if(historyPtr < 0)
        historyPtr = 0;
    if(historyPtr > history.size())
        historyPtr = history.size();
    QString cmd;
    if(historyPtr < history.size())
        cmd = history.at(historyPtr);
    ui->lineEdit->setText(cmd);
}

void RPCConsole::startExecutor()
{
    QThread *thread = new QThread;
    RPCExecutor *executor = new RPCExecutor();
    executor->moveToThread(thread);

    // Replies from executor object must go to this object
    connect(executor, SIGNAL(reply(int,QString)), this, SLOT(message(int,QString)));
    // Requests from this object must go to executor
    connect(this, SIGNAL(cmdRequest(QString)), executor, SLOT(request(QString)));

    // On stopExecutor signal
    // - queue executor for deletion (in execution thread)
    // - quit the Qt event loop in the execution thread
    connect(this, SIGNAL(stopExecutor()), executor, SLOT(deleteLater()));
    connect(this, SIGNAL(stopExecutor()), thread, SLOT(quit()));
    // Queue the thread for deletion (in this thread) when it is finished
    connect(thread, SIGNAL(finished()), thread, SLOT(deleteLater()));

    // Default implementation of QThread::run() simply spins up an event loop in the thread,
    // which is what we want.
    thread->start();
}

void RPCConsole::on_tabWidget_currentChanged(int index)
{
    if(ui->tabWidget->widget(index) == ui->tab_console)
    {
        ui->lineEdit->setFocus();
    }
    else if(clientModel && ui->tabWidget->widget(index) == ui->tab_peers)
    {
        clientModel->getPeerTableModel()->startAutoRefresh();
    }
    else if(clientModel)
    {
        clientModel->getPeerTableModel()->stopAutoRefresh();
    }

    // Issue #8 PR A — only poll the hashrate while the Mining tab is visible.
    if(miningPollTimer)
    {
        if(ui->tabWidget->widget(index) == ui->tab_mining)
        {
            updateMiningStatus();
            miningPollTimer->start();
        }
        else
        {
            miningPollTimer->stop();
        }
    }
}

// ============================================================================
// Issue #8 PR A — Mining tab (solo mode only)
// ============================================================================

void RPCConsole::on_miningEnable_toggled(bool checked)
{
    // OFFSIG window guard. If the user somehow toggles on inside the window
    // (shouldn't be possible — the checkbox is greyed — but be defensive),
    // immediately bounce it back off and log.
    if(checked && clientModel)
    {
        int h = clientModel->getNumBlocks();
        if(h >= kOffsigWindowStart && h <= kOffsigWindowEnd)
        {
            ui->miningEnable->blockSignals(true);
            ui->miningEnable->setChecked(false);
            ui->miningEnable->blockSignals(false);
            ui->miningStatus->setText(tr("Mining locked during the Codex window."));
            return;
        }
    }

    int threads = ui->miningThreads->value();
    QString cmd = checked
        ? QString("setgenerate true %1").arg(threads)
        : QString("setgenerate false");
    message(CMD_REQUEST, cmd);
    emit cmdRequest(cmd);

    updateMiningStatus();
}

void RPCConsole::on_miningThreads_valueChanged(int value)
{
    // If we're currently mining, dispatch a new setgenerate to pick up the
    // thread-count change. Otherwise the new value just sits in the spinner
    // and takes effect on the next toggle-on.
    if(ui->miningEnable->isChecked())
    {
        QString cmd = QString("setgenerate true %1").arg(value);
        message(CMD_REQUEST, cmd);
        emit cmdRequest(cmd);
    }
}

void RPCConsole::updateMiningStatus()
{
    // OFFSIG banner takes precedence — keep that line until the window passes.
    if(miningSuspendedByOffsig)
        return;

    if(!ui->miningEnable->isChecked())
    {
        ui->miningStatus->setText(tr("Mining stopped."));
        return;
    }

    double hps = dHashesPerSec;
    if(hps <= 0.0)
    {
        ui->miningStatus->setText(tr("Mining starting…"));
    }
    else if(hps < 1000.0)
    {
        ui->miningStatus->setText(tr("Mining at %1 H/s").arg(hps, 0, 'f', 1));
    }
    else if(hps < 1e6)
    {
        ui->miningStatus->setText(tr("Mining at %1 kH/s").arg(hps / 1e3, 0, 'f', 2));
    }
    else
    {
        ui->miningStatus->setText(tr("Mining at %1 MH/s").arg(hps / 1e6, 0, 'f', 2));
    }
}

// ============================================================================
// Issue #8 phase 3 — Mining tab pool mode (in-wallet stratum client).
// Talks directly to the in-process g_pStratumClient — same pattern as the
// dHashesPerSec global the solo section reads. Pool mining is deliberately
// NOT gated by the OFFSIG guard: pool blocks carry the Conclave signature.
// ============================================================================

void RPCConsole::on_poolMiningToggle_clicked(bool checked)
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

    // Replace any prior client (also covers a client left over from
    // -stratum command-line args or the setstratum RPC) with one built
    // from the form.
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

void RPCConsole::updatePoolMiningStatus()
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

void RPCConsole::updateMiningOffsigGuard(int height)
{
    bool inWindow = (height >= kOffsigWindowStart && height <= kOffsigWindowEnd);

    ui->miningOffsigBanner->setVisible(inWindow);
    ui->miningEnable->setEnabled(!inWindow);
    ui->miningThreads->setEnabled(!inWindow);

    if(inWindow)
    {
        // If solo mining was running when we entered the window, dispatch a
        // setgenerate false and remember that we did so. We don't auto-restore
        // when the window ends — the user re-enables manually post-canon.
        if(ui->miningEnable->isChecked())
        {
            ui->miningEnable->blockSignals(true);
            ui->miningEnable->setChecked(false);
            ui->miningEnable->blockSignals(false);
            QString cmd = QString("setgenerate false");
            message(CMD_REQUEST, cmd);
            emit cmdRequest(cmd);
        }
        miningSuspendedByOffsig = true;
        ui->miningStatus->setText(tr("Mining locked — Codex window in effect."));
    }
    else if(miningSuspendedByOffsig)
    {
        // Window just ended. Clear the suspended flag and refresh the status line.
        miningSuspendedByOffsig = false;
        updateMiningStatus();
    }
}

void RPCConsole::peerSelected(const QItemSelection &selected, const QItemSelection &deselected)
{
    Q_UNUSED(deselected);

    if (!clientModel || selected.indexes().isEmpty())
        return;

    const CNodeCombinedStats *stats = clientModel->getPeerTableModel()->getNodeStats(selected.indexes().first().row());
    if (stats)
        updateNodeDetail(stats);
}

void RPCConsole::peerLayoutChanged()
{
    if (!clientModel)
        return;

    const CNodeCombinedStats *stats = NULL;
    bool fUnselect = false;
    bool fReselect = false;
    bool fNewNodeSelected = false;

    if (cachedNodeid == -1) // no node selected yet
        return;

    // find the currently selected row
    int selectedRow;
    QModelIndexList selectedModelIndex = ui->peerWidget->selectionModel()->selectedIndexes();
    if (selectedModelIndex.isEmpty())
        selectedRow = -1;
    else
        selectedRow = selectedModelIndex.first().row();

    // check if our cached node is still alive
    int detectedRow = clientModel->getPeerTableModel()->getRowByNodeId(cachedNodeid);
    if (detectedRow < 0)
    {
        fUnselect = true;
        cachedNodeid = -1;
        ui->peerHeading->setText(tr("Select a peer to view detailed information."));
    }
    else
    {
        if (detectedRow != selectedRow)
        {
            fReselect = true;
            fNewNodeSelected = true;
        }
        stats = clientModel->getPeerTableModel()->getNodeStats(detectedRow);
    }

    if (fUnselect && selectedRow >= 0)
    {
        ui->peerWidget->selectionModel()->select(QItemSelection(selectedModelIndex.first(), selectedModelIndex.last()),
            QItemSelectionModel::Deselect);
    }

    if (fReselect)
    {
        ui->peerWidget->selectRow(detectedRow);
    }

    if (stats && !fNewNodeSelected)
        updateNodeDetail(stats);
}

void RPCConsole::updateNodeDetail(const CNodeCombinedStats *stats)
{
    // Update cached nodeid
    cachedNodeid = stats->nodeStats.nodeid;

    // update the detail ui with latest node information
    QString peerAddrDetails(QString::fromStdString(stats->nodeStats.addrName));
    if (!stats->nodeStats.addrLocal.empty())
        peerAddrDetails += "<br />" + tr("via %1").arg(QString::fromStdString(stats->nodeStats.addrLocal));
    ui->peerHeading->setText(peerAddrDetails);
    ui->peerServices->setText(GUIUtil::formatServicesStr(stats->nodeStats.nServices));
    ui->peerLastSend->setText(stats->nodeStats.nLastSend ? GUIUtil::formatDurationStr(GetTime() - stats->nodeStats.nLastSend) : tr("never"));
    ui->peerLastRecv->setText(stats->nodeStats.nLastRecv ? GUIUtil::formatDurationStr(GetTime() - stats->nodeStats.nLastRecv) : tr("never"));
    ui->peerBytesSent->setText(FormatBytes(stats->nodeStats.nSendBytes));
    ui->peerBytesRecv->setText(FormatBytes(stats->nodeStats.nRecvBytes));
    ui->peerConnTime->setText(GUIUtil::formatDurationStr(GetTime() - stats->nodeStats.nTimeConnected));
    ui->peerPingTime->setText(GUIUtil::formatPingTime(stats->nodeStats.dPingTime));
    ui->peerVersion->setText(QString("%1").arg(stats->nodeStats.nVersion));
    ui->peerSubversion->setText(QString::fromStdString(stats->nodeStats.cleanSubVer));
    ui->peerDirection->setText(stats->nodeStats.fInbound ? tr("Inbound") : tr("Outbound"));
    ui->peerHeight->setText(QString("%1").arg(stats->nodeStats.nStartingHeight));

    if (stats->fNodeStateStatsAvailable) {
        ui->peerBanScore->setText(QString("%1").arg(stats->nodeStateStats.nMisbehavior));
    } else {
        ui->peerBanScore->setText(tr("Fetching..."));
    }

    ui->detailWidget->setVisible(true);
}

void RPCConsole::showPeersTableContextMenu(const QPoint& point)
{
    QModelIndex idx = ui->peerWidget->indexAt(point);
    if (idx.isValid())
        peersTableContextMenu->exec(QCursor::pos());
}

void RPCConsole::copyPeerAddress()
{
    if (!clientModel) return;

    QModelIndexList selected = ui->peerWidget->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    const CNodeCombinedStats *stats =
        clientModel->getPeerTableModel()->getNodeStats(selected.first().row());
    if (!stats) return;

    QApplication::clipboard()->setText(QString::fromStdString(stats->nodeStats.addrName));
}

void RPCConsole::disconnectSelectedPeer()
{
    if (!clientModel) return;

    QModelIndexList selected = ui->peerWidget->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;

    const CNodeCombinedStats *stats =
        clientModel->getPeerTableModel()->getNodeStats(selected.first().row());
    if (!stats) return;

    NodeId target = stats->nodeStats.nodeid;

    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode *pnode, vNodes) {
        if (pnode->GetId() == target) {
            pnode->fDisconnect = true;
            break;
        }
    }
    // PeerTableModel::refresh() picks up the disappearance automatically
    // (once the socket handler thread acts on fDisconnect).
}

void RPCConsole::on_openDebugLogfileButton_clicked()
{
    GUIUtil::openDebugLogfile();
}

void RPCConsole::scrollToEnd()
{
    QScrollBar *scrollbar = ui->messagesWidget->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}

void RPCConsole::on_sldGraphRange_valueChanged(int value)
{
    const int multiplier = 5; // each position on the slider represents 5 min
    int mins = value * multiplier;
    setTrafficGraphRange(mins);
}

QString RPCConsole::FormatBytes(quint64 bytes)
{
    if(bytes < 1024)
        return QString(tr("%1 B")).arg(bytes);
    if(bytes < 1024 * 1024)
        return QString(tr("%1 KB")).arg(bytes / 1024);
    if(bytes < 1024 * 1024 * 1024)
        return QString(tr("%1 MB")).arg(bytes / 1024 / 1024);

    return QString(tr("%1 GB")).arg(bytes / 1024 / 1024 / 1024);
}

void RPCConsole::setTrafficGraphRange(int mins)
{
    ui->trafficGraph->setGraphRangeMins(mins);
    if(mins < 60) {
        ui->lblGraphRange->setText(QString(tr("%1 m")).arg(mins));
    } else {
        int hours = mins / 60;
        int minsLeft = mins % 60;
        if(minsLeft == 0) {
            ui->lblGraphRange->setText(QString(tr("%1 h")).arg(hours));
        } else {
            ui->lblGraphRange->setText(QString(tr("%1 h %2 m")).arg(hours).arg(minsLeft));
        }
    }
}

void RPCConsole::updateTrafficStats(quint64 totalBytesIn, quint64 totalBytesOut)
{
    ui->lblBytesIn->setText(FormatBytes(totalBytesIn));
    ui->lblBytesOut->setText(FormatBytes(totalBytesOut));
}

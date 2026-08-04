// Copyright (c) 2011-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "overviewpage.h"
#include "ui_overviewpage.h"

#include "bitcoinunits.h"
#include "chainparams.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "main.h"
#include "optionsmodel.h"
#include "transactionfilterproxy.h"
#include "transactiontablemodel.h"
#include "util.h"
#include "walletmodel.h"

#include <QAbstractItemDelegate>
#include <QLocale>
#include <QPainter>

#define DECORATION_SIZE 64
#define NUM_ITEMS 3

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate(): QAbstractItemDelegate(), unit(BitcoinUnits::BTC)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address);

        if(amount < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true);
        if(!confirmed)
        {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;

};
#include "overviewpage.moc"

OverviewPage::OverviewPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    cachedClaimantFinale(-1),
    txdelegate(new TxViewDelegate()),
    filter(0)
{
    ui->setupUi(this);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // init "out of sync" warning labels
    ui->labelWalletStatus->setText("(" + tr("out of sync") + ")");
    ui->labelTransactionsStatus->setText("(" + tr("out of sync") + ")");

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        emit transactionClicked(filter->mapToSource(index));
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(qint64 balance, qint64 unconfirmedBalance, qint64 immatureBalance)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balance));
    ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnit(unit, unconfirmedBalance));
    ui->labelImmature->setText(BitcoinUnits::formatWithUnit(unit, immatureBalance));
    ui->labelTotal->setText(BitcoinUnits::formatWithUnit(unit, balance + unconfirmedBalance + immatureBalance));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    ui->labelImmature->setVisible(showImmature);
    ui->labelImmatureText->setVisible(showImmature);
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());

        // The Deep: chain vitals refresh on every new block
        connect(model, SIGNAL(numBlocksChanged(int)), this, SLOT(updateChainVitals(int)));
        updateChainVitals(model->getNumBlocks());
    }
}

static QString formatHashRate(double hps)
{
    static const char* units[] = {"H/s", "kH/s", "MH/s", "GH/s", "TH/s", "PH/s"};
    int i = 0;
    while (hps >= 1000.0 && i < 5) {
        hps /= 1000.0;
        i++;
    }
    return QString("%1 %2").arg(QString::number(hps, 'f', hps < 10 ? 2 : (hps < 100 ? 1 : 0))).arg(units[i]);
}

static QString formatDifficulty(double d)
{
    return QString::number(d, 'f', d >= 100 ? 0 : (d >= 1 ? 2 : 4));
}

void OverviewPage::updateChainVitals(int count)
{
    if(!clientModel)
        return;

    QLocale loc = QLocale::system();

    ui->labelVitalsHeight->setText(loc.toString(count));
    ui->labelVitalsDiff->setText(QString("%1 / %2 %3")
        .arg(formatDifficulty(clientModel->getDifficulty()))
        .arg(formatDifficulty(clientModel->getAvgDifficulty(7 * 1440)))
        .arg(tr("avg")));
    ui->labelVitalsHash->setText(formatHashRate(clientModel->getNetworkHashPS()));

    // The Great Ritual countdown — pure height math, mirrors RitualBonus()
    CRitualStatus rs = GetRitualStatus(count);
    QString ritual;
    if (!rs.fInRite) {
        double days = rs.nBlocksToFinale / 1440.0;
        ritual = tr("block %1 — in %2 blocks (~%3 days)")
            .arg(loc.toString(rs.nNextFinale))
            .arg(loc.toString(rs.nBlocksToFinale))
            .arg(QString::number(days, 'f', days < 10 ? 1 : 0));
    } else if (rs.nBlocksToFinale == 0) {
        ritual = tr("IA! IA! The finale is THIS block — 10,000 OFF");
    } else if (rs.nRiteDay == 0) {
        ritual = tr("the finale — 10,000 OFF in %1 blocks").arg(loc.toString(rs.nBlocksToFinale));
    } else {
        QString phase;
        if (rs.nRiteDay <= 5)       phase = tr("Tharanak shagg");
        else if (rs.nRiteDay <= 12) phase = tr("fervor");
        else if (rs.nRiteDay <= 19) phase = tr("greed");
        else if (rs.nRiteDay <= 26) phase = tr("acceptance");
        else                        phase = tr("sacrifice");
        ritual = tr("the rite: %1 — finale in %2 blocks").arg(phase).arg(loc.toString(rs.nBlocksToFinale));
        if (rs.nUpcomingBounty > 0)
            ritual += tr(" · next bounty %1 OFF").arg(loc.toString((qlonglong)(rs.nUpcomingBounty / COIN)));
    }
    ui->labelVitalsRitual->setText(ritual);

    // Claimant of the most recent finale (one disk read per finale, then cached)
    if (rs.nLastFinale > 0) {
        if (cachedClaimantFinale != rs.nLastFinale) {
            std::string addr;
            if (GetCoinbasePayoutAddress(rs.nLastFinale, addr)) {
                cachedClaimantFinale = rs.nLastFinale;
                cachedClaimantAddr = QString::fromStdString(addr);
            }
        }
        if (!cachedClaimantAddr.isEmpty()) {
            QString disp = cachedClaimantAddr.left(10) + "..." + cachedClaimantAddr.right(6);
            if (Params().NetworkID() == CChainParams::MAIN)
                ui->labelVitalsClaimant->setText(QString("<a href=\"https://explorer.23skidoo.info/address/%1\">%2</a>")
                    .arg(cachedClaimantAddr).arg(disp));
            else
                ui->labelVitalsClaimant->setText(disp);
            ui->labelVitalsClaimantText->setToolTip(tr("Coinbase payout address of finale block %1. For pool-found blocks this is the pool's payout address, not the individual winner.")
                .arg(loc.toString(cachedClaimantFinale)));
            ui->labelVitalsClaimantText->setVisible(true);
            ui->labelVitalsClaimant->setVisible(true);
        }
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter = new TransactionFilterProxy();
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Status, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter);
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance());
        connect(model, SIGNAL(balanceChanged(qint64, qint64, qint64)), this, SLOT(setBalance(qint64, qint64, qint64)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if(currentBalance != -1)
            setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}

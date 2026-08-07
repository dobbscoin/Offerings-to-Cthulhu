// Copyright (c) 2011-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RPCCONSOLE_H
#define RPCCONSOLE_H

#include "guiutil.h"
#include "peertablemodel.h"

#include "net.h"

#include <QDialog>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

class ClientModel;

namespace Ui {
    class RPCConsole;
}

QT_BEGIN_NAMESPACE
class QItemSelection;
class QMenu;
QT_END_NAMESPACE

/** Local Bitcoin RPC console. */
class RPCConsole: public QDialog
{
    Q_OBJECT

public:
    explicit RPCConsole(QWidget *parent);
    ~RPCConsole();

    void setClientModel(ClientModel *model);

    enum MessageClass {
        MC_ERROR,
        MC_DEBUG,
        CMD_REQUEST,
        CMD_REPLY,
        CMD_ERROR
    };

protected:
    virtual bool eventFilter(QObject* obj, QEvent *event);

private slots:
    void on_lineEdit_returnPressed();
    void on_tabWidget_currentChanged(int index);
    /** open the debug.log from the current datadir */
    void on_openDebugLogfileButton_clicked();
    /** change the time range of the network traffic graph */
    void on_sldGraphRange_valueChanged(int value);
    /** update traffic statistics */
    void updateTrafficStats(quint64 totalBytesIn, quint64 totalBytesOut);
    /** Mining tab — issue #8 PR A (solo mode only) */
    void on_miningEnable_toggled(bool checked);
    void on_miningThreads_valueChanged(int value);
    /** Polled from a QTimer while the tab is visible; refreshes the live status line. */
    void updateMiningStatus();
    /** Mining tab — pool mode (in-wallet stratum client, issue #8 phase 3) */
    void on_poolMiningToggle_clicked(bool checked);
    void updatePoolMiningStatus();

public slots:
    void clear();
    void reject();
    void message(int category, const QString &message, bool html = false);
    /** Set number of connections shown in the UI */
    void setNumConnections(int count);
    /** Set number of blocks shown in the UI */
    void setNumBlocks(int count);
    /** OFFSIG window guard for the Mining tab — greys solo controls in [999991, 1050666]. */
    void updateMiningOffsigGuard(int height);
    /** Go forward or back in history */
    void browseHistory(int offset);
    /** Scroll console view to end */
    void scrollToEnd();
    /** Handle selection of peer in peers list */
    void peerSelected(const QItemSelection &selected, const QItemSelection &deselected);
    /** Handle updated peer information */
    void peerLayoutChanged();
    /** Show context menu on the peers table */
    void showPeersTableContextMenu(const QPoint& point);
    /** Copy the selected peer's address (addrName) to clipboard */
    void copyPeerAddress();
    /** Mark the selected peer for disconnection on the next net thread tick */
    void disconnectSelectedPeer();

signals:
    // For RPC command executor
    void stopExecutor();
    void cmdRequest(const QString &command);

private:
    static QString FormatBytes(quint64 bytes);
    void setTrafficGraphRange(int mins);
    /** show detailed information on ui about selected node */
    void updateNodeDetail(const CNodeCombinedStats *stats);

    enum ColumnWidths
    {
        NETNODEID_COLUMN_WIDTH = 50,
        ADDRESS_COLUMN_WIDTH = 200,
        SUBVERSION_COLUMN_WIDTH = 100,
        PING_COLUMN_WIDTH = 80
    };

    Ui::RPCConsole *ui;
    ClientModel *clientModel;
    QStringList history;
    int historyPtr;
    NodeId cachedNodeid;
    QMenu *peersTableContextMenu;

    /** Mining tab (issue #8 PR A) — polls getmininginfo every 2 s while the tab is visible. */
    QTimer *miningPollTimer;
    /** True when the OFFSIG window guard has forced the solo toggle off. */
    bool miningSuspendedByOffsig;

    void startExecutor();
};

#endif // RPCCONSOLE_H

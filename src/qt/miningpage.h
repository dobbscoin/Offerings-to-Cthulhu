// Copyright (c) 2026 The Offerings Conclave / SubGenius.Finance community
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MININGPAGE_H
#define MININGPAGE_H

#include <QWidget>

class ClientModel;

namespace Ui {
    class MiningPage;
}

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

/** Mining page — solo CPU mining and the in-wallet pool client (issue #8).
 *
 *  Lived as a tab inside Text Based Worship until it moved to the main
 *  window. Solo mode drives the setgenerate RPC; pool mode talks directly
 *  to the in-process g_pStratumClient. The status lines are polled every
 *  2 s, only while the page is on screen. */
class MiningPage : public QWidget
{
    Q_OBJECT

public:
    explicit MiningPage(QWidget *parent = 0);
    ~MiningPage();

    void setClientModel(ClientModel *clientModel);

protected:
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);

private slots:
    void on_miningEnable_toggled(bool checked);
    void on_miningThreads_valueChanged(int value);
    void on_poolMiningToggle_clicked(bool checked);
    void updateMiningStatus();
    void updatePoolMiningStatus();
    /** OFFSIG window guard — greys solo controls in [999991, 1050666]. */
    void updateMiningOffsigGuard(int height);

private:
    Ui::MiningPage *ui;
    ClientModel *clientModel;
    QTimer *miningPollTimer;
    /** True when the OFFSIG window guard has forced the solo toggle off. */
    bool miningSuspendedByOffsig;

    /** Run setgenerate; returns false (and shows the error) if it threw. */
    bool setGenerate(bool fGenerate, int nThreads);
};

#endif // MININGPAGE_H

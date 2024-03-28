/****************************************************************************
 *
 * QApplicationLock
 * Copyright (C) 2024 Philip Seeger <p@c0xc.net>
 *
 * This module is licensed under the MIT License.
 *
 * This is a refactored version of the locking code that
 * I wrote in 2015 for the Wallphiller program.
 *
****************************************************************************/

#ifndef QAPP_PROCESS_LOCK_HPP
#define QAPP_PROCESS_LOCK_HPP

#include <cassert>

#include <QDebug>
#include <QGuiApplication>
#include <QSharedMemory>
#include <QPointer>
#include <QTimer>
#include <QDateTime>
#include <QDataStream>
#include <QBuffer>

/**
 * QApplicationLock provides a locking mechanism for a Qt application,
 * limiting it to a single instance.
 * If instantiated with default settings,
 * the instance represents the lock and should live
 * about as long as the QApplication instance.
 *
 * The first instance creates the lock and it keeps writing a heartbeat
 * so that it won't fail to start after a crash.
 */
class QApplicationLock : public QObject
{
    Q_OBJECT

signals:

    void
    initialized();

    void
    otherInstanceDetected(qint64 pid = 0);

    void
    instanceRequested();

public:

    enum class Scope
    {
        Global  = 0,
        User    = 1 << 1,
        X11     = 1 << 2,
    };

    struct Segment
    {
        qint64 time;
        QString title;
        qint64 pid;
        bool request;
    };

    static qint64
    timestamp();

    QApplicationLock(const QString &name = "", QObject *parent = 0);

    bool
    initLock();

    /**
     * This implicitly initializes the lock and returns true if
     * this is a secondary instance, i.e., another instance is
     * actively holding and updating the lock.
     * In case of an error, it won't return true.
     */
    bool
    isSecondaryInstance();

public slots:

    void
    updateLock();

private:

    Segment
    readSegment(const QByteArray &bytes);

    Segment
    readSegment();

    bool
    writeSegment(Segment segment);

    QString
    m_name;

    bool
    m_secondary;

    int
    m_init_fail;

    QSharedMemory
    m_q_shmem;

    QTimer
    m_tmr_check;

    static constexpr int
    m_seg_size = 1024*64;

};

#endif

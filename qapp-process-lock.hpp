/****************************************************************************
 *
 * QApplicationLock
 * Copyright (C) 2025 Philip Seeger <p@c0xc.net>
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
#include <stdexcept>
#include <sys/types.h>
#include <signal.h>

#include <QtGlobal> //Q_OS_...

#if !defined(Q_OS_WIN) //system-dependent headers

#include <unistd.h> //geteuid()
#include <utime.h>

#elif defined(Q_OS_WIN)
//Windows

#include <windows.h>
#include <wtsapi32.h> //WTSGetActiveConsoleSessionId()
#include <sddl.h> //ConvertSidToStringSid()

#endif

#include <QDebug>
#include <QGuiApplication>
#include <QSharedMemory>
#include <QPointer>
#include <QTimer>
#include <QDateTime>
#include <QDataStream>
#include <QBuffer>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QSaveFile>
#include <QDir>
#include <QProcessEnvironment>
#include <QThread>

#ifdef QAPP_PROCESS_LOCK_LOG_DEBUG
#define QAPP_PROCESS_LOCK_QDEBUG qDebug()
#else
#define QAPP_PROCESS_LOCK_QDEBUG if (false) qDebug()
#endif

/**
 * QApplicationLock provides a locking mechanism for a Qt application,
 * limiting it to a single instance.
 * If instantiated with default settings,
 * the instance represents the lock and should live
 * about as long as the QApplication instance.
 *
 * The first instance creates the lock and it keeps writing a heartbeat
 * so that it won't fail to start after a crash.
 *
 * The lock instance may be created in main(), where the
 * QApplication is initialized, in the same scope.
 * It requires an event loop which is implicitly provided by QApplication.
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

    enum class Scope //: int
    {
        Undefined  = -1,
        Global  = 0,
        User    = 1 << 1,
        X11     = 1 << 2,
    };

    struct Segment
    {
        qint64 ctime;
        qint64 time;
        QString title;
        qint64 pid;
        bool request;
    };

    static qint64
    timestamp(bool milliseconds = false);

    static bool
    setFileTime(const QString &file_path, qint64 new_ts, qint64 new_ts_ms = 0);

    static QString
    getUsername(QString *user_id_str_ptr = 0);

    /**
     * Try to get identifier for desktop session.
     * This identifier can be used to distinguish a local desktop session
     * from a remote RDP session.
     */
    static QString
    getSessionId();

    /**
     * Creates a lock instance for the named application,
     * which should remain active as long as the application runs.
     *
     * The name argument must be unique.
     * If empty, the name of the application is determined and used.
     * If missing/blank, the lock cannot be created.
     * If not unique, it may interfere with other applications
     * using the same locking mechanism.
     *
     * The scope argument can be used to limit the scope of the lock.
     * With the User flag, a secondary user would be allowed to run
     * the application in parallel. Add the X11 flag to further limit
     * the scope to the X session. This can be used to allow a user
     * to have only one instance running in the desktop session :0,
     * while also allowing him to run another instance
     * in another session, for example, an XPRA session.
     * The default of -1 will create a system-global lock
     * using shared memory.
     */
    QApplicationLock(const QString &name = "", Scope scope = Scope::User, QObject *parent = 0);
    ~QApplicationLock();

    bool
    isLockActive();

    bool
    isPrimaryInstance() { return isLockActive(); };

    /**
     * This implicitly initializes the lock and returns true if
     * this is a secondary instance, i.e., another instance is
     * actively holding and updating the lock.
     * In case of an error, it won't return true.
     *
     * If an active lock (other instance running) is detected,
     * it will set the request flag before returning true.
     * The primary instance will receive it and emit a signal.
     */
    bool
    isSecondaryInstance(qint64 *pid_ptr = 0);

public slots:

    void
    updateLock();

protected:

    //void
    //debug(const QString &log_msg);

private:

    void
    initShmemName();

    void
    initFileName();

    bool
    initLockOnce();

    bool
    isProcessGone(const Segment &segment);

    bool
    isOpen() const;

    bool
    openExistingLock(bool request_write_access = false);

    Segment
    readExistingLock(bool *ok_ptr = 0, bool keep_open = false);

    bool
    createLock(const Segment &segment);

    qint64
    lockAge(Segment segment = Segment(), qint64 *last_updated_ptr = 0);

    bool
    closeLock(bool no_cleanup = false);

    Segment
    readSegment(const QByteArray &bytes, bool *ok_ptr = 0);

    Segment
    readSegment(bool *ok_ptr = 0);

    QByteArray
    serializeSegment(const Segment &segment);

    bool
    writeSegment(const QByteArray &bytes);

    bool
    writeFile(const QByteArray &bytes);

    bool
    writeLock(const QByteArray &bytes);

    bool
    writeLock(const Segment &segment);

    QString
    m_name;

    bool
    m_active = false;

    bool
    m_secondary = false;

    qint64
    m_primary_pid = 0;

    int
    m_init_fail = false;

    int
    m_scope;

    bool
    m_initialized = false;

    bool
    m_use_shmem = false;

    bool
    m_use_file = false;

    QString
    m_lock_filename;

    QSharedMemory
    m_q_shmem;

    QFile
    m_lock_file;

    QFileInfo
    m_lock_file_info;

    QTimer
    m_tmr_check;

    qint64
    m_lock_file_last_updated = 0;

    static constexpr int
    m_seg_size = 1024*64;

};

#endif

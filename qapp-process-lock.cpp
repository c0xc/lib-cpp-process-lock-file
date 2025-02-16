#include "qapp-process-lock.hpp"

qint64
QApplicationLock::timestamp(bool milliseconds)
{
    qint64 current_time = 0;
    if (!milliseconds)
    {
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
        current_time = QDateTime::currentSecsSinceEpoch();
#else
        current_time = QDateTime::currentMSecsSinceEpoch() / 1000;
#endif
    }
    else
    {
        current_time = QDateTime::currentMSecsSinceEpoch();
    }
    return current_time;
}

bool
QApplicationLock::setFileTime(const QString &file_path, qint64 new_ts, qint64 new_ts_ms)
{
    bool ok = false;

#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)

    QDateTime new_date = QDateTime::fromSecsSinceEpoch(new_ts);
    if (new_ts_ms)
        new_date = QDateTime::fromMSecsSinceEpoch(new_ts_ms);
    QFile file(file_path);
    file.open(QFile::ReadOnly); //required for setFileTime()
    ok = file.setFileTime(new_date, QFileDevice::FileModificationTime);

#else

    /*
      The utimes() system call is similar, but the times argument refers
      to an array rather than a structure.  The elements of this array
      are timeval structures, which allow a precision of 1 microsecond
      for specifying timestamps.
    */

    //Convert qint64 timestamp to time_t
    time_t new_mtime = static_cast<time_t>(new_ts);

    //Struct
    time_t atime = new_mtime;
    struct utimbuf st_times;
    st_times.actime = atime;
    st_times.modtime = new_mtime;

    //Apply the new times to the file
    if (utime(file_path.toStdString().c_str(), &st_times) == 0)
    {
        ok = true;
    }
    else
    {
        QAPP_PROCESS_LOCK_QDEBUG << "Failed to set modification time for" << file_path;
    }

#endif

    return ok;
}

QString
QApplicationLock::getUsername()
{
    QString username;

#if defined(Q_OS_UNIX)

    uid_t uid = geteuid();

    username = QString(getlogin());

#elif defined(Q_OS_WIN)

    //??
    char username_c[MAX_USERNAME];
    DWORD username_int = sizeof(username_c);
    if (!GetUserName(username_c, &username_int))
    {
        throw ...
    }
    username = QString(username_c());

#endif

    return username;
}

QString
QApplicationLock::getSessionId()
{
    QString sid;

    //TODO os switch?
    //These are Linux/X variables
    //because this scope functionality is intended for Linux X sessions

    sid = QProcessEnvironment::systemEnvironment().value("XDG_SESSION_ID");

    QString display_id = QProcessEnvironment::systemEnvironment().value("DISPLAY");

    return "";
}

QApplicationLock::QApplicationLock(const QString &name, Scope scope, QObject *parent)
                : QObject(parent),
                  m_secondary(false),
                  m_init_fail(0)
{
    //Use developer-defined application name as lock name - MUST BE UNIQUE
    m_name = name; //"REPLACE NAME"
    if (m_name.isEmpty())
    {
        QCoreApplication *app = QCoreApplication::instance();
        if (app)
            m_name = app->applicationName();
    }
    if (m_name.isEmpty())
    {
        throw std::invalid_argument("name argument missing (unique application name)");
    }

    //NOTE LIMITATION: file mode only possible in user scope
    //For a system-global lock, instead of files (unreliable, permissions),
    //we'd have to use shared memory (if/as long supported, see other note)
    //or a local socket.

    //Determine scope and lock mode, prepare lock (lock won't be activated yet)
    m_scope = (int)scope;
    if (scope == Scope::Undefined || scope == Scope::Global) m_use_shmem = true;
    else m_use_file = true;
    if (m_use_file) initFileName();
    if (m_use_shmem) initShmemName();

    //Heartbeat timer
    int update_interval = m_use_file ? 3000 : 1000;
    m_tmr_check.setInterval(update_interval);
    connect(&m_tmr_check, SIGNAL(timeout()), SLOT(updateLock()));

}

QApplicationLock::~QApplicationLock()
{
    if (isLockActive()) closeLock();
}

bool
QApplicationLock::isLockActive()
{
    return m_active;
}

bool
QApplicationLock::isSecondaryInstance(qint64 *pid_ptr)
{
    //Try to acquire lock
    initLockOnce();
    //Return secondary flag, set if existing + active lock found
    //Otherwise, this instance will now have the lock
    if (pid_ptr) *pid_ptr = m_primary_pid;
    return m_secondary;
}

void
QApplicationLock::updateLock()
{

    if (m_use_shmem)
    {
        //Read shmem segment
        Segment seg = readSegment();

        //Update heartbeat
        seg.time = timestamp(true);

        if (seg.request)
        {
            //Request signal received (flag was set)
            QAPP_PROCESS_LOCK_QDEBUG << "qapp-lock: request flag detected";
            emit instanceRequested();
            //Reset flag
            seg.request = false;
        }

        //Write shmem segment
        writeSegment(serializeSegment(seg));
    }
    else if (m_use_file)
    {
        //Check lock file timestamp from metadata
        m_lock_file_info.refresh(); //discard cached timestamp!
        QDateTime file_date = m_lock_file_info.lastModified().toUTC();
        qint64 file_mtime = m_lock_file_info.lastModified().toUTC().toMSecsSinceEpoch();

        //Re-read file only if timestamp differs from our last known timestamp
        if (!m_lock_file_last_updated || file_mtime != m_lock_file_last_updated)
        {
            QAPP_PROCESS_LOCK_QDEBUG << "qapp-lock: checking/reading lock";
            bool ok = false;
            Segment seg = readExistingLock(&ok, false); //false: don't close it

            if (ok && seg.request)
            {
                //Request signal received (flag was set)
                QAPP_PROCESS_LOCK_QDEBUG << "qapp-lock: request flag detected";
                emit instanceRequested();
                //Reset flag
                seg.request = false;
                writeFile(serializeSegment(seg));
            }
        }

        //Update file timestamp
        qint64 ts_ms = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch(); //toSecsSinceEpoch() >= Qt 5.8
        if (setFileTime(m_lock_file.fileName(), ts_ms / 1000, ts_ms))
        {
            m_lock_file_last_updated = ts_ms;
            QAPP_PROCESS_LOCK_QDEBUG << "qapp-lock: timestamp updated to" << ts_ms;
        }
        else
        {
            m_lock_file_last_updated = 0;
        }
    }

}

void
QApplicationLock::initShmemName()
{
    assert(!m_q_shmem.isAttached());

    m_q_shmem.setKey(m_name);
}

void
QApplicationLock::initFileName()
{
    QString filename;

    //Get session properties
    QString uid_str = getUsername();
    QString sid_str = getSessionId(); //TODO extend for other platforms

    //Make lock name, unique for application + [user/session]
    filename = "(QApplicationLock)";
    filename += m_name;
    if (m_scope & (int)Scope::User)
        filename += QString("|%1").arg(uid_str);
    else
        throw std::invalid_argument("system-global scope not supported in file mode");
    if (m_scope & (int)Scope::X11)
        filename += QString("|%1").arg(sid_str);

    //Encode to avoid problematic characters ("/!\n") ending up in a filename
    filename = filename.toUtf8().toBase64();
    filename = QString(".%1.lck").arg(filename);

    //Set lock file name, path
    //If used in a Flatpak sandbox, consider adjusting lock_dir:
    //If an application uses $TMPDIR to contain lock files you may want to add a wrapper script that sets it to $XDG_RUNTIME_DIR/app/$FLATPAK_ID (tmpfs) or /var/tmp (persistent on host).
    //https://docs.flatpak.org/en/latest/sandbox-permissions.html
    QString lock_dir = QDir::tempPath(); //env $TMPDIR or /tmp
    m_lock_filename = filename;
    assert(!m_lock_file.isOpen()); //init must run after ctor before locking
    QString file_path = QDir(lock_dir).filePath(filename);
    m_lock_file_info.setFile(file_path);
    m_lock_file.setFileName(file_path);
}

bool
QApplicationLock::initLockOnce()
{
    //When first called, this will try to initialize the lock,
    //so it'll create it and start the timer that will check and update it.
    //On success, this will be the primary instance.
    //If an active lock is found, we'll abort without starting the timer
    //because this would be a secondary instance then.
    if (m_initialized) return false;
    m_initialized = true;

    //Code from 2015, 9 years ago:

    //The shared memory segment contains a "request" flag (boolean),
    //which is used by a second instance to tell the first instance
    //to show up. The first instance will check this flag periodically.
    //If a shared memory segment is found, another instance is probably
    //already running, so the flag is set and this instance terminates.
    //However, a "last update" timestamp is also stored, which is used
    //to determine if the memory segment is actually a dead leftover
    //from a previously killed instance. In this case, it will be discarded.

    //Initially determine if there is an instance with the same lock
    //If the previous instance crashed, there may be an old leftover.
    //If it crashed within the last < timeout seconds,
    //then we're assuming another instance is running even though it's gone,
    //in which case the user should restart the program after < timeout sec.
    //In shmem mode, we could detect this by merely calling openExistingLock()
    //again and if that fails, the leftover was automatically cleaned up.
    //Checking the process using kill would only be possible in user mode.
    bool found_lock = false;
    Segment seg = readExistingLock(&found_lock);
    if (found_lock)
    {
        int timeout = 15;
        qint64 age = lockAge(seg) / 1000;
        bool is_proc_gone = isProcessGone(seg);

        //Check if lock is old or active
        if (age > timeout || is_proc_gone)
        {
            //Too old, it's a leftover
            QAPP_PROCESS_LOCK_QDEBUG << "Found old process lock, discarding" << "age:" << age << "process gone:" << is_proc_gone;

            //Detach, ignore dead leftover
            closeLock(); //delete lock

            //Continue
        }
        else
        {
            //It's an active memory segment
            //Other instance is running
            QAPP_PROCESS_LOCK_QDEBUG << "Another instance is already running";
            QAPP_PROCESS_LOCK_QDEBUG << "heartbeat age:" << age << "pid:" << seg.pid;
            m_secondary = true;

            //Request first instance (set show flag)
            seg.request = true;
            if (openExistingLock(true)) //open for writing
                writeLock(seg);
            //else reattaching failed, ignore that error

            //Only this flag is changed from false to true!
            //The size of the whole segment does not change!
            //So no need to worry about overflowing.

            //Explicitly detach (just to make it obvious that we're done)
            //Detach/close lock (without removing it)
            closeLock(true); //detach, in file mode close without removing it

            //Prevent this instance from breaking config
            //dont_touch_config = true; //was that a good idea?
            m_primary_pid = seg.pid;
            emit otherInstanceDetected(seg.pid);

            //Terminate
            return false;
        }
    }

    //Set primary instance flag
    m_active = true;

    //Write segment with lock info
    seg = Segment{};
    seg.ctime = timestamp(true); //creation time
    //seg.time = 0 //heartbeat updated by timer routine
    seg.pid = QCoreApplication::applicationPid();
    seg.request = false;
    //Write, create lock
    if (!createLock(seg))
    {
        QAPP_PROCESS_LOCK_QDEBUG << "failed to create process lock";
        return false;
    }
    QAPP_PROCESS_LOCK_QDEBUG << "process lock created";

    //Start update timer
    m_tmr_check.start();
    updateLock();

    return true;
}

bool
QApplicationLock::isProcessGone(const Segment &segment)
{
    //Try to check if the primary process is still running
    //This is not always possible
    //We're checking the process itself in user mode (other user => no perms)

    if (m_scope & (int)Scope::User)
    {
        //In user mode, a 0 signal to the primary process is used
        //to determine if it's still running
        //(ignoring another process with the same pid)
        if (kill(segment.pid, 0) != 0)
        {
            //No such process anymore
            return true;
        }

        //The primary process is running (or, in case of an error, another one)
        //Ideally use another identifier like the process name to double-check
        //(that it's not a new process with the same pid after primary crashed)
        //uptime might be better because process name can change
    }

    return false; //default response - it's not gone or we don't know
}

bool
QApplicationLock::isOpen() const
{
    if (m_use_shmem)
        return m_q_shmem.isAttached();
    else if (m_use_file)
        return m_lock_file.isOpen();
    else
        return false;
}

bool
QApplicationLock::openExistingLock(bool request_write_access)
{
    bool ok = false;

    //Open *existing* lock, fails if lock does not exist yet
    if (m_use_shmem)
    {
        if (m_q_shmem.isAttached()) m_q_shmem.detach(); //close read-only fd
        if (request_write_access)
            ok = m_q_shmem.attach();
        else
            ok = m_q_shmem.attach(QSharedMemory::ReadOnly);
        if (!ok)
        {
            QAPP_PROCESS_LOCK_QDEBUG << "Attaching to shared memory segment failed";
            QAPP_PROCESS_LOCK_QDEBUG << m_q_shmem.errorString();
        }
    }
    else if (m_use_file)
    {
        //Reopen (if lock file has been replaced)
        if (m_lock_file.isOpen()) m_lock_file.close();
        //Open file, if it exists! Return false otherwise
        //Note: In WriteOnly or ReadWrite mode, if the relevant file does not already exist, this function will try to create a new file before opening it.
        auto mode = request_write_access ? QFile::ReadWrite : QFile::ReadOnly;
        if (m_lock_file_info.exists()) //avoid creating empty file
            ok = m_lock_file.open(mode);
    }

    return ok;
}

QApplicationLock::Segment
QApplicationLock::readExistingLock(bool *ok_ptr, bool keep_open)
{
    //Open/load and read, return lock, if it exists (ok = true)
    //Otherwise set ok = false
    //Immediately close it unless keep_open is true
    //Return content as it is, timestamp might be 0 if file metadata
    //is used to update heartbeat
    Segment seg{};
    bool ok = false;
    ok = isOpen() || openExistingLock();

    //Read lock, parse and return if valid
    //A missing end mark makes it invalid
    if (!ok) {} //close, return false
    else if (m_use_shmem)
    {
        //Lock, read, unlock using Qt's QSharedMemory
        seg = readSegment(&ok);
    }
    else if (m_use_file)
    {
        //Read lock file, if found and non-empty
        //Lock file should not be written to at the same time
        //to avoid reading incomplete file
        m_lock_file.seek(0);
        QByteArray bytes = m_lock_file.readAll();
        seg = readSegment(bytes, &ok);

        //If timestamp is 0, the file's mtime is the last update timestamp
        if (!seg.time || true) //always using metadata in file mode
        {
            qint64 file_mtime = m_lock_file_info.lastModified().toMSecsSinceEpoch();
            seg.time = file_mtime;
        }
    }

    //Close it (e.g., to allow peeking without actually starting the timer)
    if (!keep_open)
    {
        closeLock(true);
    }

    if (ok_ptr) *ok_ptr = ok;
    return seg;
}

bool
QApplicationLock::createLock(const Segment &segment)
{
    bool ok = false;

    if (m_use_shmem)
    {
        //Create and attach to shmem lock, which should not exist at this point
        if (m_q_shmem.isAttached()) m_q_shmem.detach();
        //create + attach, fails if it already exists
        ok = m_q_shmem.create(m_seg_size);
        if (!ok && m_q_shmem.error() == QSharedMemory::AlreadyExists)
        {
            QAPP_PROCESS_LOCK_QDEBUG << "failed to create shmem lock because it already exists";
            ok = m_q_shmem.attach();
        }
        else if (!ok)
        {
            QAPP_PROCESS_LOCK_QDEBUG << "Creating shared memory segment failed";
            QAPP_PROCESS_LOCK_QDEBUG << m_q_shmem.errorString();
        }
    }
    else if (m_use_file)
    {
        //Create, open lock file for writing
        if (m_lock_file.isOpen()) m_lock_file.close();
        ok = m_lock_file.open(QFile::ReadWrite);
    }

    ok = ok && writeLock(segment);

    return ok;
}

qint64
QApplicationLock::lockAge(Segment segment, qint64 *last_updated_ptr)
{
    //Get mtime, last updated (ms)
    qint64 lock_time = segment.time;
    if (m_use_file)
    {
        //In file mode, file mtime is used to avoid rewriting the file every 1s
        if (!segment.time)
        {
            m_lock_file_info.refresh(); //discard cached timestamp!
            qint64 file_mtime = m_lock_file_info.lastModified().toUTC().toMSecsSinceEpoch();
            lock_time = file_mtime;
        }
    }

    qint64 age = 0;
    if (lock_time)
        age = timestamp(true) - lock_time;

    if (last_updated_ptr) *last_updated_ptr = lock_time;
    return age;
}

bool
QApplicationLock::closeLock(bool no_cleanup)
{
    bool close_ok = false;

    if (m_use_shmem)
    {
        close_ok = m_q_shmem.detach();
    }
    else if (m_use_file)
    {
        m_lock_file.close();
        close_ok = true;
        if (!no_cleanup) m_lock_file.remove();
    }

    return close_ok;
}

QApplicationLock::Segment
QApplicationLock::readSegment(const QByteArray &bytes, bool *ok_ptr)
{
    Segment seg{};
    bool ok = false;

    QChar e = 0;
    qint8 n = 0;
    QDataStream stream(bytes);
    stream
    >> seg.time
    >> seg.title
    >> seg.pid
    >> seg.request
    >> n; //end mark
    e = n;

    if (e == 'E')
    {
        ok = true;
    }
    else
    {
        //end mark not found, incomplete read
        seg = Segment();
        QAPP_PROCESS_LOCK_QDEBUG << "failed to read process lock, incomplete data";
    }

    if (ok_ptr) *ok_ptr = ok;
    return seg;
}

QApplicationLock::Segment
QApplicationLock::readSegment(bool *ok_ptr)
{
    m_q_shmem.lock();
    QBuffer shmem_buffer_in;
    shmem_buffer_in.setData((char*)m_q_shmem.constData(),
        m_q_shmem.size());
    m_q_shmem.unlock();

    return readSegment(shmem_buffer_in.data(), ok_ptr);
}

QByteArray
QApplicationLock::serializeSegment(const Segment &segment)
{
    QBuffer shmem_buffer_out;
    shmem_buffer_out.open(QBuffer::ReadWrite);
    QDataStream stream(&shmem_buffer_out);
    stream << segment.time;
    stream << segment.title;
    stream << segment.pid;
    stream << segment.request;
    stream << (qint8)'E'; //end mark
    QByteArray bytes = shmem_buffer_out.data();

    return bytes;
}

bool
QApplicationLock::writeSegment(const QByteArray &bytes)
{
    assert(m_q_shmem.isAttached());

    const char *from = bytes.data();
    assert(bytes.size() < m_seg_size);

    m_q_shmem.lock();
    char *to = (char*)m_q_shmem.data();
    memcpy(to, from, bytes.size()); //or m_q_shmem.size()
    m_q_shmem.unlock();

    return true;
}

bool
QApplicationLock::writeFile(const QByteArray &bytes)
{

    //Note that the reader must reopen the replaced file
    //rename():
    //This functionality is intended to support materializing the destination file with all contents already present, so another process cannot see an incomplete file in the process of being written.
    bool write_ok = false;

    //Write via temp file and replace lock file (atomic)
    QSaveFile save_file(m_lock_file.fileName());
    save_file.open(QIODevice::WriteOnly);
    write_ok = save_file.write(bytes) == bytes.size();
    write_ok = write_ok && save_file.commit();

    //Write directly into lock file (not very atomic)
    if (0 /* not atomic */)
    {
        m_lock_file.seek(0);
        m_lock_file.write(bytes);
        m_lock_file.seek(0);
    }

    return write_ok;
}

bool
QApplicationLock::writeLock(const QByteArray &bytes)
{
    bool ok = false;

    if (m_use_shmem)
    {
        //Lock, read, unlock using Qt's QSharedMemory
        ok = writeSegment(bytes);
    }
    else if (m_use_file)
    {
        ok = writeFile(bytes);
    }

    return ok;
}

bool
QApplicationLock::writeLock(const Segment &segment)
{
    return writeLock(serializeSegment(segment));
}


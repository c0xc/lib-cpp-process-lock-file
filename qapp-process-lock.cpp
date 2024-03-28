#include "qapp-process-lock.hpp"

qint64
QApplicationLock::timestamp()
{
    qint64 current_time = QDateTime::currentMSecsSinceEpoch() / 1000;
    return current_time;
}

QApplicationLock::QApplicationLock(const QString &name, QObject *parent)
                : QObject(parent),
                  m_secondary(false),
                  m_init_fail(0)
{
    m_name = name; //"REPLACE NAME"
    if (m_name.isEmpty())
    {
        QCoreApplication *app = QCoreApplication::instance();
        if (app)
            m_name = app->applicationName();
    }

    m_tmr_check.setInterval(1000);
    connect(&m_tmr_check, SIGNAL(timeout()), SLOT(updateLock()));
}

bool
QApplicationLock::initLock()
{
    assert(!m_q_shmem.isAttached());
    m_q_shmem.setKey(m_name);

    //Code from 2015, 9 years ago:

    //The shared memory segment contains a "request" flag (boolean),
    //which is used by a second instance to tell the first instance
    //to show up. The first instance will check this flag periodically.
    //If a shared memory segment is found, another instance is probably
    //already running, so the flag is set and this instance terminates.
    //However, a "last update" timestamp is also stored, which is used
    //to determine if the memory segment is actually a dead leftover
    //from a previously killed instance. In this case, it will be discarded.
    //TODO IDEA access request flag directly without QDataStream
    //We serialize and deserialize the whole memory segment using QDataStream
    //every couple of seconds. This is overkill.
    //We should get rid of QDataStream and the flag should be checked
    //directly, for example by defining it do be at position 99.
    //Really, reading, parsing, writing the whole segment every 5 seconds
    //is useless overhead. But it should work for now and will be fixed.

    //Initially determine if there is an instance with the same lock
    //Fails if there is none, may also fail if the other process is stopped...
    if (m_q_shmem.attach())
    {
        //Found and attached to existing memory segment

        //Check time, how old is it?
        Segment seg = readSegment();
        int age = timestamp() - seg.time;
        int timeout = 15;

        if (age > timeout)
        {
            //Too old, it's a leftover
            qDebug() << "Found old memory segment, discarding" << "age:" << age;

            //Detach, ignore dead leftover
            if (!m_q_shmem.detach())
            {
                qWarning() << "Detaching failed";
            }

            //Continue
        }
        else
        {
            //It's an active memory segment
            //Other instance is running
            qWarning() << "Another instance is already running";
            qDebug() << "heartbeat age:" << age << "pid:" << seg.pid;
            m_secondary = true;

            //Request first instance (set show flag)
            Segment seg = readSegment();
            seg.request = true;
            writeSegment(seg);
            m_q_shmem.detach();
            emit otherInstanceDetected(seg.pid);

            //Only this flag is changed from false to true!
            //The size of the whole segment does not change!
            //So no need to worry about overflowing.

            //Explicitly detach (just to make it obvious that we're done)
            m_q_shmem.detach();

            //Prevent this instance from breaking config
            //dont_touch_config = true; //was that a good idea?

            //Terminate
            //if (qapp_ptr) QApplication::closeAllWindows() ...
            return false;
        }
    }

    //First instance
    if (!m_q_shmem.create(m_seg_size))
    {
        //Creating shared memory segment failed
        qWarning() << "Creating shared memory segment failed";
        qWarning() << m_q_shmem.errorString();
        m_init_fail = 1;

        if (!m_q_shmem.attach())
            return false;
        //Now what?
    }
    //Write segment with lock info
    Segment seg{};
    seg.time = timestamp();
    seg.pid = QCoreApplication::applicationPid();
    seg.request = false;
    writeSegment(seg);

    //Start update timer
    m_tmr_check.start();

    return true;
}

bool
QApplicationLock::isSecondaryInstance()
{
    if (!m_q_shmem.isAttached())
    {
        initLock();
    }

    return m_secondary;
}

void
QApplicationLock::updateLock()
{
    //Read
    Segment seg = readSegment();

    //Update heartbeat
    seg.time = timestamp();

    //Check for request
    if (seg.request)
    {
        emit instanceRequested();
        seg.request = false;
    }

    //Write
    writeSegment(seg);
}

QApplicationLock::Segment
QApplicationLock::readSegment(const QByteArray &bytes)
{
    Segment seg{};

    QDataStream stream(bytes);
    stream
    >> seg.time
    >> seg.title
    >> seg.pid
    >> seg.request;

    return seg;
}

QApplicationLock::Segment
QApplicationLock::readSegment()
{
    m_q_shmem.lock();
    QBuffer shmem_buffer_in;
    shmem_buffer_in.setData((char*)m_q_shmem.constData(),
        m_q_shmem.size());
    m_q_shmem.unlock();

    return readSegment(shmem_buffer_in.data());
}

bool
QApplicationLock::writeSegment(Segment segment)
{
    QBuffer shmem_buffer_out;
    shmem_buffer_out.open(QBuffer::ReadWrite);
    QDataStream stream(&shmem_buffer_out);
    stream << segment.time;
    stream << segment.title;
    stream << segment.pid;
    stream << segment.request;
    QByteArray bytes = shmem_buffer_out.data();
    const char *from = bytes.data();
    assert(bytes.size() < m_seg_size);

    m_q_shmem.lock();
    char *to = (char*)m_q_shmem.data();
    //memcpy(to, from, m_q_shmem.size());
    memcpy(to, from, bytes.size());
    m_q_shmem.unlock();

    return true;
}


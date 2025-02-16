#include "main.hpp"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("qapp-process-lock-sample");

    QApplicationLock lock;
    //QApplicationLock lock("", QApplicationLock::Scope::Global);
    qint64 pid = 0;
    if (lock.isSecondaryInstance(&pid))
    {
        QMessageBox::information(0, "program already running",
            QString("This program is already running with pid %1.").arg(pid));
        return 0;
    }

    MainWindow *gui = new MainWindow;
    QObject::connect(&lock, SIGNAL(instanceRequested()), gui, SLOT(showInstance()));
    gui->show();
    int code = app.exec();
    delete gui;
    return code;
}

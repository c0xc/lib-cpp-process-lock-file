#include "mainwindow.hpp"

MainWindow::MainWindow(QWidget *parent)
          : QDialog(parent)
{
    QLabel *lbl_proc_info = new QLabel(QString("This is process %1").arg(QCoreApplication::applicationPid()));

    QVBoxLayout *vbox = new QVBoxLayout;
    vbox->addWidget(lbl_proc_info);
    setLayout(vbox);

}

void
MainWindow::showInstance()
{
    //Try to highlight this instance's window
    //Note that raising a window is not always possible
    //Maybe also prefix the title until the window has been focused
    //This is just an example to show how the request signal can be used
    show();
    setWindowState( //unminimize
        (windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise(); //for Mac?
    activateWindow(); //for Windows
}


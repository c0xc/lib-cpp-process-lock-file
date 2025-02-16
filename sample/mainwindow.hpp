#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QDebug>
#include <QCoreApplication>
#include <QDialog>
#include <QLabel>
#include <QVBoxLayout>

class MainWindow : public QDialog
{
    Q_OBJECT

public:

    MainWindow(QWidget *parent = 0);

public slots:

    void
    showInstance();

};

#endif

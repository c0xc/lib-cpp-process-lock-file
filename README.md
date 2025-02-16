lib-cpp-process-lock-file
===

This is not a program but a C++ module.
It's meant to be included as a submodule, then symlinked.

This module provides an easy way to restrict the application
to a single instance.
If a second instance is started, the first instance will be activated,
preventing multiple instances being started accidentally.



Usage
---

To include this module directly into the source,
simply add it as a submodule:

    $ git submodule add https://github.com/c0xc/lib-cpp-process-lock-file.git
    $ ln -s lib-cpp-process-lock-file/qapp-process-lock.hpp inc/
    $ ln -s lib-cpp-process-lock-file/qapp-process-lock.cpp src/



Module
---

This module provides an easy way to restrict the application
to a single instance.
If a second instance is started, the first instance will be requested,
preventing multiple instances being started accidentally.

Some alternative solutions exist but most of them are either complicated,
rely on unsafe assumptions or simply fail if the program crashes,
so the program can't remove the lock.

The Qt version of this module has been developed for Qt 5
and can be used by instantiating its class.

There are multiple scopes.
In global mode, which is the original implementation,
a shared memory segment is used for locking.
In user mode, a temporary file is used for locking.
A third mode using a local socket might be considered in the future.

The initial Qt version is actually based on code I wrote in 2015
for the Wallphiller program (0072ee79).



Usage
---

Add this to your main() function (or wherever you initially create the
the QApplication object):

    QApplication app(argc, argv);

    // >>>
    QApplicationLock lock("UNIQUE_APPLICATION_NAME");
    if (lock.isSecondaryInstance())
        return 0;
    // <<<

    MainWindow *gui = new MainWindow;
    gui->show();
    app.exec();

The lock object should be in the same scope as the application object,
so that it lives as long as the application.

isSecondaryInstance() will implicitly try to initialize the lock
and if that fails because there's already an active lock, it returns true.
In user scope (new default), this would happen if the same user
already had another instance of this program running.
Instances of other users would be ignored.
Use global scope to ensure that only one user can have the program running
at any given time, and any attempts by other users to run it again will fail.

To use global mode (shared memory), initialize the lock like this:

    QApplicationLock lock("UNIQUE_APPLICATION_NAME", QApplicationLock::Scope::Global);

If you want to allow multiple instances per user, but only one per X session,
set the X11 scope:

    QApplicationLock lock("UNIQUE_APPLICATION_NAME", QApplicationLock::Scope::User | QApplicationLock::Scope::X11);

The X11 scope can be used to allow only one instance in the local display
session :0, another one in a remote XPRA session :10 etc.
If the display id is not available, it will fall back to user scope.



Author
------

Philip Seeger (philip@c0xc.net)



License
---

Redistribution and usage is granted, as long as the author is mentioned.

See LICENSE.

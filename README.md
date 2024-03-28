lib-cpp-process-lock-file
===

This is not a program but a C++ module.
It's meant to be included as a submodule, then symlinked.

Module
---

This module provides an easy way to restrict the application
to a single instance.
If a second instance is started, the first instance will be activated,
preventing multiple instances being started accidentally.

Some alternative solutions exist but most of them are either complicated,
rely on unsafe assumptions or simply fail if the program crashes,
so the program can't remove the lock.

The Qt version of this module has been developed for Qt 5
and can be used by instantiating its class.

We may or may not add the other implementation of this module,
which does not depend on Qt, to this repository later.

The initial Qt version is actually based on code I wrote 2015 for Wallphiller.



License
---

Redistribution and usage is granted, as long as the author is mentioned.

See LICENSE.

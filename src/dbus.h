#ifndef __DBUS_H__
#define __DBUS_H__

#include "ds_state.h"


void
register_dbus (GDBusConnection  *connection,
               const char       *name,
               DsState          *state);

#endif //__DBUS_H__

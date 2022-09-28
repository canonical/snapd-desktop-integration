#ifndef __DBUS_H__
#define __DBUS_H__

#include "ds_state.h"

bool register_dbus (GDBusConnection  *connection,
                    DsState          *state,
                    GError          **error);


#endif //__DBUS_H__

#ifndef EXAMPLE_USER_SERVICE_H
#define EXAMPLE_USER_SERVICE_H

#include "../../serviceProv.h"
#include "iuser.h"

namespace example {

// The factory is the only layer that needs to know concrete service classes.
SERVICE_PROVIDER(UserService, IUser);

} // namespace example

#endif

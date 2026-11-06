#include "examples/service_provider/request_types.h"
#include "examples/service_provider/user_service.h"

#include <iostream>
#include <vector>

void printIds(
    example::UserService& userService,
    const CONTEXT& context,
    const char* label) {
    const auto ids = userService.callMethodGet(context, &example::IUser::getId);
    std::cout << label;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (index != 0) {
            std::cout << ", ";
        }
        std::cout << ids[index];
    }
    std::cout << '\n';
}

int main() {
    using namespace example;

    // 1. Create the provider. It owns the service instances and their lifecycle.
    UserService userService;

    // 2. A Context is a shared grouping key. Instances registered with this
    //    context will receive the same broadcast calls.
    service::Context request("request-42");

    // 3. Request types are compile-time tags. No StaffRequest or NormalRequest
    //    objects are needed or kept alive; the factory uses typeid on the type.

    // The first registration creates and returns the shared Context handle.
    const auto context = userService.addInstance<StaffRequest>(request);

    // Reuse that handle to put a second implementation in the same group.
    userService.addInstance<NormalRequest>(context);

    // 4. callMethod broadcasts setId(21) to every IUser in this context.
    userService.callMethod(context, &IUser::setId, 21);

    // 5. callMethodGet also broadcasts, then returns one result per instance
    //    in registration order: staff first, normal second.
    const std::vector<int> ids = userService.callMethodGet(context, &IUser::getId);

    std::cout << "Staff ID: " << ids.at(0) << '\n';
    std::cout << "Normal ID: " << ids.at(1) << '\n';

    // Other code does not need a global provider: pass the same provider by
    // reference to a function, while passing the context handle separately.
    printIds(userService, context, "Access through a function: ");

    // findContext only looks up an existing name; it never creates a context.
    const auto recoveredContext = userService.findContext("request-42");
    if (recoveredContext) {
        printIds(userService, recoveredContext, "Found named context: ");
    }
    if (!userService.findContext("missing-request")) {
        std::cout << "Missing context was not created by lookup.\n";
    }

    // A different name creates an independent group in the same provider.
    service::Context backgroundTask("background-task");
    const auto backgroundContext = userService.addInstance<NormalRequest>(backgroundTask);
    userService.callMethod(backgroundContext, &IUser::setId, 7);
    printIds(userService, backgroundContext, "Independent context: ");

    // The caller-object overload is also available. It reads the caller's type
    // while addInstance runs; it does not retain a reference to that object.
    service::Context callerBasedRequest("caller-based-request");
    CONTEXT callerBasedContext;
    {
        StaffRequest shortLivedCaller;
        callerBasedContext = userService.addInstance(callerBasedRequest, shortLivedCaller);
    }
    userService.callMethod(callerBasedContext, &IUser::setId, 13);
    printIds(userService, callerBasedContext, "After caller leaves scope: ");

    // Remove all groups explicitly. The provider itself remains alive until
    // main exits; another UserService object would own a separate registry.
    userService.erase(*context);
    userService.erase(*backgroundContext);
    userService.erase(*callerBasedContext);
}
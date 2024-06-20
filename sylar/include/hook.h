#include "fiber.h"
#include "IOManager.h"
#include "thread"

#define HOOK_FUN(XX) \

 XX(sleep) \

 XX(usleep) \

 XX(nanosleep) \

 XX(socket) \

 XX(connect) \

 XX(accept) \
上⾯的宏展开之后的效果如下：

 XX(read) \

 XX(readv) \

 XX(recv) \

 XX(recvfrom) \

 XX(recvmsg) \

 XX(write) \

 XX(writev) \

 XX(send) \

 XX(sendto) \

 XX(sendmsg) \

 XX(close) \

 XX(fcntl) \

 XX(ioctl) \

 XX(getsockopt) \

 XX(setsockopt)

 

extern "C" {

#define XX(name) name ## _fun name ## _f = nullptr;

 HOOK_FUN(XX);

#undef XX

}
 

void hook_init() {
 static bool is_inited = false;
 if(is_inited) {
 return;
 }

#define XX(name) name ## _f = (name ## _fun)dlsym(RTLD_NEXT, #name);

 HOOK_FUN(XX);

#undef XX

}

extern "C" {
 sleep_fun sleep_f = nullptr; \

 usleep_fun usleep_f = nullptr; \

 ....
 setsocketopt_fun setsocket_f = nullptr;
};
 

hook_init() {
 
 sleep_f = (sleep_fun)dlsym(RTLD_NEXT, "sleep"); \

 usleep_f = (usleep_fun)dlsym(RTLD_NEXT, "usleep"); \

 setsocketopt_f = (setsocketopt_fun)dlsym(RTLD_NEXT, "setsocketopt");
}



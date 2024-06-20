#include <signal.h>
#include <stack>
#include<fiber.h>
// 上下⽂结构体定义

// 这个结构体是平台相关的，因为不同平台的寄存器不⼀样

// 下⾯列出的是所有平台都⾄少会包含的4个成员

typedef struct ucontext_t {
 // 当前上下⽂结束后，下⼀个激活的上下⽂对象的指针，只在当前上下⽂是由makecontext创建时有效

 struct ucontext_t *uc_link;
 // 当前上下⽂的信号屏蔽掩码

 sigset_t uc_sigmask;
 // 当前上下⽂使⽤的栈内存空间，只在当前上下⽂是由makecontext创建时有效

 stack_t uc_stack;
 // 平台相关的上下⽂具体内容，包含寄存器的值

 mcontext_t uc_mcontext;

} ucontext_t;
 

// 获取当前的上下⽂

int getcontext(ucontext_t *ucp);
 

// 恢复ucp指向的上下⽂，这个函数不会返回，⽽是会跳转到ucp上下⽂对应的函数中执⾏，相当于变相调⽤了函数

int setcontext(const ucontext_t *ucp);
 

// 修改由getcontext获取到的上下⽂指针ucp，将其与⼀个函数func进⾏绑定，⽀持指定func运⾏时的参数，

// 在调⽤makecontext之前，必须⼿动给ucp分配⼀段内存空间，存储在ucp->uc_stack中，这段内存空间将作为

// func函数运⾏时的栈空间，

// 同时也可以指定ucp->uc_link，表示函数运⾏结束后恢复uc_link指向的上下⽂，

// 如果不赋值uc_link，那func函数结束时必须调⽤setcontext或swapcontext以重新指定⼀个有效的上下⽂，
// 否则程序就跑⻜了

// makecontext执⾏完后，ucp就与函数func绑定了，调⽤setcontext或swapcontext激活ucp时，func就会被
// 运⾏

void makecontext(ucontext_t *ucp, void (*func)(), int argc, ...);
 

// 恢复ucp指向的上下⽂，同时将当前的上下⽂存储到oucp中，

// 和setcontext⼀样，swapcontext也不会返回，⽽是会跳转到ucp上下⽂对应的函数中执⾏，相当于调⽤了函数

// swapcontext是sylar⾮对称协程实现的关键，线程主协程和⼦协程⽤这个接⼝进⾏上下⽂切换

int swapcontext(ucontext_t *oucp, const ucontext_t *ucp);
/// 线程局部变量，当前线程正在运⾏的协程

static thread_local Fiber *t_fiber = nullptr;

/// 线程局部变量，当前线程的主协程，切换到这个协程，就相当于切换到了主线程中运⾏，智能指针形式

static thread_local Fiber::ptr t_thread_fiber = nullptr;
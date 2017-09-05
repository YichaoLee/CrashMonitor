

#include "NativeCrashHandler.h"

#include <jni.h>    // 使用jni进行java和c语言互相调用,必须导入的头文件
#include <string>
#include <stdlib.h>    // <stdlib.h> 头文件里包含了C语言的中最常用的系统函数
#include <signal.h>    // 在signal.h头文件中，提供了一些函数用以处理执行过程中所产生的信号。
#include <unistd.h>
#include <android/log.h> // 谷歌提供的用于安卓JNI输出log日志的头文件
#include <cxxabi.h>
#include <dlfcn.h>
#include <iomanip>
#include <assert.h>
#include <sys/system_properties.h>
#include <ucontext.h>
#include <unwind.h>
#include <fstream>
#include <sstream>
#include <iomanip>
//#include "libunwind/libunwind.h"
//#include "libunwind/unwind.h"


#ifndef NDEBUG
#define Verify(x, r)  {if(!x) LOGE(r);}
#else
#define Verify(x, r)  ((void)(x))
#endif

#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, "CrashMonitor", __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG , "CrashMonitor", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO , "CrashMonitor", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN , "CrashMonitor", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR , "CrashMonitor", __VA_ARGS__)

#if defined(REG_RIP)
# define SIGSEGV_STACK_IA64
# define REGFORMAT "%016lx"
#elif defined(REG_EIP)
# define SIGSEGV_STACK_X86
# define REGFORMAT "%08x"
#else
# define SIGSEGV_STACK_GENERIC
# define REGFORMAT "%x"
#endif

# define likely(x)  __builtin_expect(!!(x), 1)
# define unlikely(x)    __builtin_expect(!!(x), 0)

extern "C"
{

const int handle_signals[]={
        SIGILL,     // 信号4   非法指令
        SIGABRT,    // 信号6   来自abort函数的终止信号
        SIGBUS,     // 信号7   总线错误
        SIGFPE,    // 信号8   浮点异常
        SIGSEGV,    // 信号11   无效的存储器引用(段故障)
#ifdef SIGSTKFLT
        SIGSTKFLT,// 信号16 协处理器上的栈故障
#endif
        SIGPIPE};   //信号13   向一个没有读用户的管道做写操作

const int signal_num = sizeof(handle_signals)/ sizeof(handle_signals[0]);
int androidversion;

static struct sigaction old_sa[NSIG];

static jclass applicationClass = 0;
static jmethodID makeCrashReportMethod;
static jmethodID makeCrashReportMethod2;
static jobject applicationObject = 0;

static jclass stackTraceElementClass = 0;
static jmethodID stackTraceElementMethod;

static JavaVM *javaVM;
//手机信息
static const char* infostr;
const char* fileName;
static jstring stackInfo;

const size_t BACKTRACE_FRAMES_MAX = 128;

pthread_mutex_t mutex_;
pthread_mutex_t handled_mutex_;
pthread_cond_t cond_;
pthread_cond_t handled_cond_;
bool signaled_ = false;
bool signal_handled = false;

int global_signal_no;
struct siginfo *global_siginfo;
void *global_sigcontext;
int global_pid = -1;

/**
 * libcorkscrew库的函数和数据结构
 */
typedef struct map_info_t map_info_t;
typedef struct {
    uintptr_t absolute_pc;
    uintptr_t stack_top;
    size_t stack_size;
} backtrace_frame_t;
typedef struct {
    uintptr_t relative_pc;
    uintptr_t relative_symbol_addr;
    char *map_name;
    char *symbol_name;
    char *demangled_name;
} backtrace_symbol_t;
typedef ssize_t (*t_unwind_backtrace_signal_arch)(siginfo_t *si, void *sc, const map_info_t *lst,
                                                  backtrace_frame_t *bt, size_t ignore_depth,
                                                  size_t max_depth);
static t_unwind_backtrace_signal_arch unwind_backtrace_signal_arch;
typedef map_info_t *(*t_acquire_my_map_info_list)();
static t_acquire_my_map_info_list acquire_my_map_info_list;
typedef void (*t_release_my_map_info_list)(map_info_t *milist);
static t_release_my_map_info_list release_my_map_info_list;
typedef void (*t_get_backtrace_symbols)(const backtrace_frame_t *backtrace, size_t frames,
                                        backtrace_symbol_t *symbols);
static t_get_backtrace_symbols get_backtrace_symbols;
typedef void (*t_free_backtrace_symbols)(backtrace_symbol_t *symbols, size_t frames);
static t_free_backtrace_symbols free_backtrace_symbols;

#if defined(__i386__)
#define STACKCALL __attribute__((regparm(1),noinline))
void ** STACKCALL getEBP(void){
    void **ebp=NULL;
    __asm__ __volatile__("mov %%ebp, %0;\n\t"
    :"=m"(ebp)      /* 输出 */
    :      /* 输入 */
    :"memory");     /* 不受影响的寄存器 */
    return (void **)(*ebp);
}
#endif

int my_backtrace(void **buffer,int size, unsigned long pc){
#if defined(__i386__)
    int frame=0;
    void ** ebp;
    void **ret=NULL;
    unsigned long long func_frame_distance=0;
    if(buffer!=NULL && size >0)
    {
        ebp=(void **)pc;
        func_frame_distance=(unsigned long long)(*ebp) - (unsigned long long)ebp;
        while(ebp&& frame<size
              &&(func_frame_distance< (1ULL<<24))//assume function ebp more than 16M
              &&(func_frame_distance>0))
        {
            ret=ebp+1;
            buffer[frame++]=*ret;
            ebp=(void**)(*ebp);
            func_frame_distance=(unsigned long long)(*ebp) - (unsigned long long)ebp;
        }
    }
    return frame;
#elif defined(__arm__)
    int frame=0;
    void ** ebp;
    void **ret=NULL;
    unsigned long long func_frame_distance=0;
    if(buffer!=NULL && size >0)
    {
        ebp=(void **)pc;
        func_frame_distance=(unsigned long long)ebp-(unsigned long long)(*ebp);
        while(ebp&& frame<size
              &&(func_frame_distance< (1ULL<<24))//assume function ebp more than 16M
              &&(func_frame_distance>0))
        {
            ret=ebp-1;
            buffer[frame++]=*ret;
            ebp=(void**)(*(ebp-3));
            func_frame_distance=(unsigned long long)(*ebp) - (unsigned long long)ebp;
        }
    }
    return frame;
#endif
}

struct BacktraceState
{
    void** current;
    void** end;
};

static _Unwind_Reason_Code unwindCallback(struct _Unwind_Context* context, void* arg)
{
    BacktraceState* state = static_cast<BacktraceState*>(arg);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc) {
        if (state->current == state->end) {
            return _URC_END_OF_STACK;
        } else {
            *state->current++ = reinterpret_cast<void*>(pc);
        }
    }
    return _URC_NO_REASON;
}

size_t captureBacktrace(void** buffer, size_t max, unsigned long pc)
{
    BacktraceState state = {buffer, buffer + max};
    _Unwind_Backtrace(unwindCallback, &state);

    return state.current - buffer;
}

inline void dumpBacktrace(const char *reason,void** addrs, size_t count)
{
    const void *preAddr = 0;
    std::ofstream out(fileName,std::ios_base::app);
    out.write(infostr,strlen(infostr));
    out<<"Error type: "<<reason<<std::endl;
    for (size_t idx = 4; idx < count; ++idx) {
        const void* addr = addrs[idx];
        const char* symbol = "";

        Dl_info info;
        if (!dladdr(addr, &info) ){
            LOGE("dladdr error: %s.",dlerror());
        }
        else {
            const char *symbol = info.dli_sname;
            int status = 0;
            char *demangled = __cxxabiv1::__cxa_demangle(symbol, 0, 0, &status);

            if(preAddr!=addr) {
                preAddr = addr;
            }
            else
                break;

            const char* map_name = info.dli_fname ? info.dli_fname : "<unknown>";
            const char* symbol_name = (NULL != demangled && 0 == status) ? demangled : (symbol != NULL)?symbol:"(null)";
            int offset = (uintptr_t)addr - (uintptr_t)info.dli_saddr;
            LOGE("#%02d pc %08X %s ((%s)+%d)",
                 idx-4,
                 addr,
                 info.dli_fname ? info.dli_fname : "<unknown>",
                 (NULL != demangled && 0 == status) ? demangled : symbol,
                 (uintptr_t)addr - (uintptr_t)info.dli_saddr);

            out<<"#"<<std::setfill('0')<<std::setw(2)<<idx-4<<" pc "<<std::hex<<std::setw(8)<<addr<<" "<<map_name<<"("<<symbol_name<<"+"<<std::dec<<offset<<")"<<std::endl;

            if (NULL != demangled)
                free(demangled);
        }
    }
    out.close();
}


//int slow_backtrace (void **buffer, int size, unw_context_t *uc)
//{
//    unw_cursor_t cursor;
//    unw_word_t ip;
//    int n = 0;
//
//    if (unlikely (unw_init_local (&cursor, uc) < 0))
//        return 0;
//
//    while (unw_step (&cursor) > 0)
//    {
//        if (n >= size)
//            return n;
//        if (unw_get_reg (&cursor, UNW_REG_IP, &ip) < 0)
//            return n;
//        buffer[n++] = (void *) (uintptr_t) ip;
//    }
//    return n;
//}


/**
 * 读取libcorkscrew库
 */
void init(){
    void * libcorkscrew = dlopen("libcorkscrew.so", RTLD_LAZY | RTLD_LOCAL);
    if (libcorkscrew) {
        unwind_backtrace_signal_arch = (t_unwind_backtrace_signal_arch) dlsym(
                libcorkscrew, "unwind_backtrace_signal_arch");
        acquire_my_map_info_list = (t_acquire_my_map_info_list) dlsym(
                libcorkscrew, "acquire_my_map_info_list");
        release_my_map_info_list = (t_release_my_map_info_list) dlsym(
                libcorkscrew, "release_my_map_info_list");
        get_backtrace_symbols = (t_get_backtrace_symbols) dlsym(libcorkscrew,
                                                                "get_backtrace_symbols");
        free_backtrace_symbols = (t_free_backtrace_symbols) dlsym(libcorkscrew,
                                                                  "free_backtrace_symbols");
    }
}
/**
 *
 */
void getLowLevelStack(const char *reason, struct siginfo *siginfo, void *sig_context){
    init();

    backtrace_frame_t frames[BACKTRACE_FRAMES_MAX] = {0,};
    backtrace_symbol_t symbols[BACKTRACE_FRAMES_MAX] = {0,};

    map_info_t* const info = acquire_my_map_info_list();
    const ssize_t size = unwind_backtrace_signal_arch(siginfo, sig_context, info, frames, 0, BACKTRACE_FRAMES_MAX);
    get_backtrace_symbols(frames, size, symbols);

    std::ofstream out(fileName,std::ios_base::app);
    out.write(infostr,strlen(infostr));
    out<<"Error type: "<<reason<<std::endl;
    for (int i = 0; i < size; i++)
    {
        backtrace_symbol_t& symbol = symbols[i];

        const char* map_name = symbol.map_name ? symbol.map_name : "<unknown>";
        const char* symbol_name = symbol.demangled_name ? symbol.demangled_name : symbol.symbol_name;
        int offset = symbol.relative_pc - symbol.relative_symbol_addr;

        LOGE("#%02d pc %08X  %s (%s+%d)",
             i,
             symbol.relative_pc,
             symbol.map_name ? symbol.map_name : "<unknown>",
             symbol.demangled_name ? symbol.demangled_name : symbol.symbol_name,
             symbol.relative_pc - symbol.relative_symbol_addr);

        out<<"#"<<std::setfill('0')<<std::setw(2)<<i<<" pc "<<std::hex<<std::setw(8)<<symbol.relative_pc<<" "<<map_name<<"("<<symbol_name<<"+"<<std::dec<<offset<<")"<<std::endl;
    }
    out.close();
    free_backtrace_symbols(symbols, size);
    release_my_map_info_list(info);
}

/**
 * 使用alarm保证信号处理程序不会陷入死锁或者死循环
 * */
static void signal_pre_handler(const int signalno, siginfo_t *const si,
                               void *const sc) {

    /* Ensure we do not deadlock. Default of ALRM is to die.
    * (signal() and alarm() are signal-safe) */
    signal(signalno, SIG_DFL);
    signal(SIGALRM, SIG_DFL);

    /* Ensure we do not deadlock. Default of ALRM is to die.
      * (signal() and alarm() are signal-safe) */
    (void) alarm(8);
}


/**
 * 获取崩溃线程名，用于回调java层获取堆栈
 */
char* getThreadName(pid_t tid) {
    int THREAD_NAME_LENGTH = 128;
    if (tid <= 1) {
        return NULL;
    }
    char* path = (char *) calloc(1, 80);
    char* line = (char *) calloc(1, THREAD_NAME_LENGTH);

    snprintf(path, PATH_MAX, "proc/%d/status", tid);
    FILE* commFile = NULL;
    if (commFile = fopen(path, "r")) {

        fgets(line, THREAD_NAME_LENGTH, commFile);
        fclose(commFile);
    }
    free(path);
    if (line) {
        int length = strlen(line);
        if (line[length - 1] == '\n') {
            line[length - 1] = '\0';
        }
    }
    return line;
}

/**
 * 中断异常的上报函数，将native层的堆栈信息以jstring的格式返回给java层
 * */
std::string _makeNativeCrashReport(const char *reason, struct siginfo *siginfo, void *sig_context, int pid) {

    JNIEnv *env;
    LOGE("Native crash %s occurred.",reason);
    int result = javaVM->GetEnv((void **) &env, JNI_VERSION_1_6);

    if (result == JNI_EDETACHED) {
        LOGW("Native crash occured in a non jvm-attached thread");
        result = javaVM->AttachCurrentThread(&env, NULL);
    }

    if (result != JNI_OK)
        LOGE("Could not attach thread to Java VM for crash reporting.\n"
                     "Crash was: %s",
             reason);
    else
    if (env && applicationObject) {

        LOGE("Native crash reporting\n");

        LOGE("%s",infostr);

        if (androidversion < 21) {
            /**
             * 如果android版本小于21，利用libcorkscrew获取native层堆栈信息。
             */
//            LOGW("Android version is less than 21.");
            getLowLevelStack(reason, siginfo, sig_context);
        }
        else if(androidversion  >= 21) {

            /**
            * 如果android版本大于21，利用libbacktrace获取native层堆栈信息。
            */
//            LOGW("Android version is larger than 21.");

//            unw_context_t uc;
//            //platform specific voodoo to build a context for libunwind
//#if defined(__arm__)
//            LOGW("base pc: ");
//            LOGW("%08X", (void *) sig_context);
//            LOGW("\n");
//            //cast/extract the necessary structures
//            ucontext_t *context = (ucontext_t *) sig_context;
//            unw_tdep_context_t *unw_ctx = (unw_tdep_context_t *) &uc;
//            sigcontext *sig_ctx = &context->uc_mcontext;
//            //we need to store all the general purpose registers so that libunwind can resolve
//            //    the stack correctly, so we read them from the sigcontext into the unw_context
//            unw_ctx->regs[UNW_ARM_R0] = sig_ctx->arm_r0;
//            unw_ctx->regs[UNW_ARM_R1] = sig_ctx->arm_r1;
//            unw_ctx->regs[UNW_ARM_R2] = sig_ctx->arm_r2;
//            unw_ctx->regs[UNW_ARM_R3] = sig_ctx->arm_r3;
//            unw_ctx->regs[UNW_ARM_R4] = sig_ctx->arm_r4;
//            unw_ctx->regs[UNW_ARM_R5] = sig_ctx->arm_r5;
//            unw_ctx->regs[UNW_ARM_R6] = sig_ctx->arm_r6;
//            unw_ctx->regs[UNW_ARM_R7] = sig_ctx->arm_r7;
//            unw_ctx->regs[UNW_ARM_R8] = sig_ctx->arm_r8;
//            unw_ctx->regs[UNW_ARM_R9] = sig_ctx->arm_r9;
//            unw_ctx->regs[UNW_ARM_R10] = sig_ctx->arm_r10;
//            unw_ctx->regs[UNW_ARM_R11] = sig_ctx->arm_fp;
//            unw_ctx->regs[UNW_ARM_R12] = sig_ctx->arm_ip;
//            unw_ctx->regs[UNW_ARM_R13] = sig_ctx->arm_sp;
//            unw_ctx->regs[UNW_ARM_R14] = sig_ctx->arm_lr;
//            unw_ctx->regs[UNW_ARM_R15] = sig_ctx->arm_pc;
//#elif defined(__i386__)
//            ucontext_t* context = (ucontext_t*)sig_context;
//            //on x86 libunwind just uses the ucontext_t directly
//            uc = *((unw_context_t*)context);
//#endif
//            LOGW("set context done.");

//            ucontext_t* uc = (ucontext_t*)sig_context;

//            int i = 0;
//            void **frame_pointer = (void **)NULL;
//            void *return_address = NULL;
//            void *pc = NULL;
//            Dl_info dl_info = { 0 };
//#if (defined __i386__)
//            frame_pointer = (void **)uc->uc_mcontext.gregs[REG_EBP];
//            pc = return_address = (void *)uc->uc_mcontext.gregs[REG_EIP];
//#elif (defined __arm__)
//            frame_pointer = (void **)uc->uc_mcontext.arm_fp;
//            pc = (void *)uc->uc_mcontext.arm_pc;
//            return_address = (void *)uc->uc_mcontext.arm_lr;
//#endif
//
//            LOGE("Stack trace:");
//            while (frame_pointer && pc) {
//                if (!dladdr(pc, &dl_info)) {
//                    LOGE("dladdr error: %s.", dlerror());
//                    break;
//                }
//                const char *sname = dl_info.dli_sname;
//                int status;
//                char *tmp = __cxxabiv1::__cxa_demangle(sname, NULL, 0, &status);
//                if (status == 0 && tmp) {
//                    sname = tmp;
//                }
//                LOGE("%02d: %p (%s) <%s + %lu> ",
//                     i++,
//                     pc,
//                     dl_info.dli_fname,
//                     sname,
//                     (unsigned long)return_address - (unsigned long)dl_info.dli_saddr
//                      );
//                if (tmp)
//                    free(tmp);
//
//#if (defined __i386__)
//                pc = return_address = frame_pointer[1];
//                frame_pointer = (void **)frame_pointer[0];
//#elif (defined __arm__)
//                LOGE("%08X",return_address);
//                for(int i=-20;i<20;++i)
//                    LOGE("%02d: %08X\t%08X",i,&frame_pointer[i],frame_pointer[i]);
//                pc = return_address;
//                frame_pointer = (void **)frame_pointer[0];
//                return_address = *(frame_pointer-2);
//                LOGE("%08X",return_address);
//#endif
//            }
//            LOGE("Stack trace end.");

            /* 获取调用栈 */
            ucontext_t* uc = (ucontext_t*)sig_context;
#if (defined __i386__)
            unsigned long esp =  uc->uc_mcontext.gregs[REG_ESP];
            unsigned long ip = uc->uc_mcontext.gregs[REG_EIP];
#elif (defined __arm__)
            unsigned long esp = uc->uc_mcontext.arm_fp;
            unsigned long ip = uc->uc_mcontext.arm_pc;
#endif
            void *stack[BACKTRACE_FRAMES_MAX] = {0};

            size_t size = captureBacktrace(stack, BACKTRACE_FRAMES_MAX, ip);
            dumpBacktrace(reason, stack,size);

//            int size = my_backtrace(stack, BACKTRACE_FRAMES_MAX,esp);
//            for(int i=0;i<size;++i){
//                void *addr = stack[i];
//                Dl_info info;
//                if (!dladdr(addr, &info) ){
//                    LOGE("dladdr error: %s.",dlerror());
//                }
//                else {
//                    const char *symbol = info.dli_sname;
//                    int status = 0;
//                    char *demangled = __cxxabiv1::__cxa_demangle(symbol, 0, 0, &status);
//
//                    LOGE("#%02d pc %08X %s ((%s)+%d)",
//                         i,
//                         addr,
//                         info.dli_fname ? info.dli_fname : "<unknown>",
//                         (NULL != demangled && 0 == status) ? demangled : symbol,
//                         (uintptr_t)addr - (uintptr_t)info.dli_saddr);
//
//                    if (NULL != demangled)
//                        free(demangled);
//                }
//            }
//            unw_cursor_t cursor;
//            unw_word_t ip,sp,offp;
//            char funcName[1024];
//            int n = 0;
//
//            if (unlikely (unw_init_local(&cursor, &uc) < 0))
//                return ;
//
//            do{
//                offp = 0;
//                if (n >= BACKTRACE_FRAMES_MAX)
//                    return;
//                if (unw_get_reg(&cursor, UNW_REG_IP, &ip) < 0)
//                    return ;
//                unw_get_reg(&cursor, UNW_REG_SP, &sp);
//                unw_get_proc_name(&cursor, funcName, 1024, &offp);
//
//                const void* addr = reinterpret_cast<void *>((uintptr_t)ip);
//
//                Dl_info info;
//                if (!dladdr(addr, &info) ){
//                    LOGE("dladdr error: %s.",dlerror());
//                }
//                else {
//                    const char *symbol = info.dli_sname;
//                    int status = 0;
//                    char *demangled = __cxxabiv1::__cxa_demangle(symbol, 0, 0, &status);
//
//                    LOGE("#%02d pc %08X %s ((%s)+%d)",
//                         n++,
//                         addr,
//                         info.dli_fname ? info.dli_fname : "<unknown>",
//                         (NULL != demangled && 0 == status) ? demangled : symbol,
//                         offp);
//
//                    if (NULL != demangled)
//                        free(demangled);
//                }
//            }while (unw_step(&cursor) > 0);
        }

        LOGW("Native stacks got.");
        /**
         * 回调java层
         */
//        env->CallVoidMethod(applicationObject, makeCrashReportMethod, stackInfo,
//                            env->NewStringUTF(threadName), (jint)pid);

//        LOGW("Java recalled..");
        if(env->ExceptionCheck() != JNI_FALSE) {
            LOGE("Java threw an exception in makeCrashReportMethod.");
            jthrowable mException = NULL;
            mException = env->ExceptionOccurred();
            if (mException != NULL) {
                env->Throw(mException);
                //最后别忘了清除异常，不然还是会导致VM崩溃
                env->ExceptionClear();
            }
        }

    } else
        LOGE("Could not create native crash report as registerForNativeCrash was not called in JAVA context.\n"
                     "Crash was: %s",
             reason
        );
}

/*
 * 中断异常的信号处理函数
 * */
void nativeCrashHandler_sigaction(int signal, struct siginfo *siginfo, void *sigcontext) {


    //执行原处理函数
    if (old_sa[signal].sa_handler)
        old_sa[signal].sa_handler(signal);

    signal_pre_handler(signal, siginfo, sigcontext);

    global_signal_no = signal;
    global_sigcontext = sigcontext;
    global_siginfo = siginfo;
    global_pid = getpid();

    JNIEnv *env = 0;
    int result = javaVM->GetEnv((void **) &env, JNI_VERSION_1_6);

    if (result == JNI_EDETACHED) {
        LOGW("Native crash occured in a non jvm-attached thread");
        result = javaVM->AttachCurrentThread(&env, NULL);
}

    if(env->ExceptionCheck() != JNI_FALSE) {
        LOGE("Java threw an exception in makeCrashReportMethod.");
        jthrowable mException = NULL;
        mException = env->ExceptionOccurred();

        if (mException != NULL) {
            env->Throw(mException);
            //最后别忘了清除异常，不然还是会导致VM崩溃
            env->ExceptionClear();
        }
    }
    //检测到信号，唤醒获取堆栈线程
//    pthread_mutex_lock(&mutex_);
//    signaled_ = true;
//    pthread_cond_signal(&cond_);
//    pthread_mutex_unlock(&mutex_);
//    LOGW("wakeup signal.");
//
//    //挂起自身等待堆栈获取完成
//    pthread_mutex_lock(&handled_mutex_);
//    while(!signal_handled)
//        pthread_cond_wait(&handled_cond_, &handled_mutex_);
//    pthread_mutex_unlock(&handled_mutex_);
    _makeNativeCrashReport(strsignal(signal), siginfo, sigcontext, global_pid);
    LOGW("Native crash handled.");

    _exit(-1);
}


void waitForSignal(){
    pthread_mutex_lock(&mutex_);
    while(!signaled_)
        pthread_cond_wait(&cond_, &mutex_);
    pthread_mutex_unlock(&mutex_);
}

void notifyThrowException(){
    pthread_mutex_lock(&handled_mutex_);
    signal_handled = true;
    pthread_cond_broadcast(&handled_cond_);
    pthread_mutex_unlock(&handled_mutex_);
}

void throw_exception(){
    _makeNativeCrashReport(strsignal(global_signal_no), global_siginfo, global_sigcontext, global_pid);
}

/**
 * 获取堆栈的线程函数
 */
void* DumpThreadEntry(void *argv) {
    JNIEnv* env = NULL;
    int estatus = 1;
    Verify(javaVM,"javaVM is null.");
    if(javaVM->AttachCurrentThread(&env, NULL) != JNI_OK)
    {
        LOGE("AttachCurrentThread() failed");
        estatus = 0;
        return &estatus;
    }
    //等待信号处理函数唤醒
    waitForSignal();

    //获取native异常堆栈并回调java层函数
    throw_exception();

    //告诉信号处理函数已经处理完了
    notifyThrowException();

    if(javaVM->DetachCurrentThread() != JNI_OK)
    {
        LOGE("DetachCurrentThread() failed");
        estatus = 0;
        return &estatus;
    }

    return &estatus;
}

JNIEXPORT jboolean JNICALL
Java_monitor_NativeCrash_NativeCrashHandler_nRegisterForNativeCrash(JNIEnv *env, jobject obj) {

    if (applicationClass) {
        applicationObject = (jobject) env->NewGlobalRef(obj);
        Verify(applicationObject,
               "Could not create NativeCrashHandler java object global reference");
        /**
         * 创建一个新线程获取堆栈
         */
        pthread_t thd;
        pthread_cond_init(&cond_, NULL);
        pthread_mutex_init(&mutex_, NULL);
        pthread_cond_init(&handled_cond_, NULL);
        pthread_mutex_init(&handled_mutex_, NULL);
        int ret = pthread_create(&thd, NULL, DumpThreadEntry, NULL);
        if(ret) {
            LOGE("pthread_create error");
            return 0;
        }
        return 1;
    }

    return 0;
}

JNIEXPORT void JNICALL
Java_monitor_NativeCrash_NativeCrashHandler_nUnregisterForNativeCrash(JNIEnv *env, jobject) {
    if (applicationObject) {
        env->DeleteGlobalRef(applicationObject);
    }
}

JNIEXPORT jint JNI_OnLoad(JavaVM *jvm, void *reserved) {

    char value[10];
    __system_property_get("ro.build.version.sdk", value);
    androidversion = atoi(value);

    javaVM = jvm;
    JNIEnv *env = NULL;
    /*JavaVM::GetEnv 原型为 jint (*GetEnv)(JavaVM*, void**, jint);
         * GetEnv()函数返回的  Jni 环境对每个线程来说是不同的，
         *  由于Dalvik虚拟机通常是Multi-threading的。每一个线程调用JNI_OnLoad()时，
         *  所用的JNI Env是不同的，因此我们必须在每次进入函数时都要通过vm->GetEnv重新获取
         *
         */
    //得到JNI Env
    int result = jvm->GetEnv((void **) &env, JNI_VERSION_1_6);
    Verify(result == JNI_OK, "Could not get JNI environment");

    applicationClass = env->FindClass("monitor/NativeCrash/NativeCrashHandler");
    Verify(applicationClass, "Could not find NativeCrashHandler java class");
    applicationClass = (jclass)env->NewGlobalRef(applicationClass);
    Verify(applicationClass, "Could not create NativeCrashHandler java class global reference");
    makeCrashReportMethod = env->GetMethodID(applicationClass, "makeCrashReport",
                                             "(Ljava/lang/String;Ljava/lang/String;I)V");
    Verify(makeCrashReportMethod, "Could not find makeCrashReport java method");

    makeCrashReportMethod2 = env->GetMethodID(applicationClass, "makeCrashReport",
                                              "()V");
    Verify(makeCrashReportMethod2, "Could not find makeCrashReport2 java method");

    /**
     * 获取手机型号等信息
     */
    jclass monitorclass = env->FindClass("monitor/MonitorUtil");
    Verify(monitorclass, "Could not find MonitorUtil java class");
    monitorclass = (jclass)env->NewGlobalRef(monitorclass);
    Verify(monitorclass, "Could not create MonitorUtil java class global reference");
    jmethodID infoMethod = env->GetStaticMethodID(monitorclass, "getPackageInfo","()Ljava/lang/String;");
    Verify(infoMethod, "Could not find getPackageInfo java method");
    jobject packageInfo = env->CallStaticObjectMethod(monitorclass,infoMethod);
    infostr = env->GetStringUTFChars(jstring(packageInfo), false);

    /**
     * 获取Log本地日志路径
     */
    jmethodID fileMethod = env->GetStaticMethodID(monitorclass, "getFilePath","()Ljava/lang/String;");
    Verify(infoMethod, "Could not find getFilePath java method");
    jobject fileInfo = env->CallStaticObjectMethod(monitorclass,fileMethod);
    fileName = env->GetStringUTFChars(jstring(fileInfo), false);

    /**
     * 删除临时全局变量
     */
    env->DeleteGlobalRef(monitorclass);

    if(env->ExceptionCheck() != JNI_FALSE) {
        LOGE("Java threw an exception in makeCrashReportMethod.");
        jthrowable mException = NULL;
        mException = env->ExceptionOccurred();

        if (mException != NULL) {
            env->Throw(mException);
            //最后别忘了清除异常，不然还是会导致VM崩溃
            env->ExceptionClear();
        }
    }
    //如果发生堆栈错误，那么在堆栈上运行的信号处理函数会一直造成堆栈溢出的错误
    stack_t stack;
    memset(&stack, 0, sizeof(stack));
    // 设置额外的堆栈运行处理函数
    stack.ss_size = SIGSTKSZ;
    stack.ss_sp = malloc(stack.ss_size);
    Verify(stack.ss_sp, "Could not allocate signal alternative stack");
    stack.ss_flags = 0;
    // 加载设置的额外堆栈
    result = sigaltstack(&stack, NULL);
    Verify(!result, "Could not set signal stack");


    struct sigaction handler;    // struct定义结构体(类似于java中的javabean)
    // c库<string.h>下的函数,void *memset(void *buffer, int c, int count); 把buffer所指内存区域的前count个字节设置成字符c
    // 参1:指向要填充的内存块。
    // 参2:要被设置的值。该值以 int 形式传递，但是函数在填充内存块时是使用该值的无符号字符形式。
    // 用于获取任何东西的内存大小
    // 参3:要被设置为该值的字节数。
    memset(&handler, 0, sizeof(struct sigaction));
    sigemptyset(&handler.sa_mask);

    //        // 结构体sigaction包含了对特定信号的处理、信号所传递的信息、信号处理函数执行过程中应屏蔽掉哪些函数等等。
    //        struct sigaction {
    //            void     (*sa_handler)(int); // 指定对signum信号的处理函数，可以是SIG_DFL默认行为，SIG_IGN忽略接送到的信号，或者一个信号处理函数指针。这个函数只有信号编码一个参数。
    //            void     (*sa_sigaction)(int, siginfo_t *, void *); // 当sa_flags中存在SA_SIGINFO标志时，sa_sigaction将作为signum信号的处理函数。
    //            sigset_t   sa_mask;    // 指定信号处理函数执行的过程中应被阻塞的信号。
    //            int        sa_flags; // 指定一系列用于修改信号处理过程行为的标志，由0个或多个标志通过or运算组合而成，比如SA_RESETHAND，SA_ONSTACK | SA_SIGINFO。
    //            void     (*sa_restorer)(void); // 已经废弃，不再使用。
    //        }

    // 设置信号处理函数
    handler.sa_sigaction = nativeCrashHandler_sigaction;
    // 信号处理之后重新设置为默认的处理方式。
    //    SA_RESTART：使被信号打断的syscall重新发起。
    //    SA_NOCLDSTOP：使父进程在它的子进程暂停或继续运行时不会收到 SIGCHLD 信号。
    //    SA_NOCLDWAIT：使父进程在它的子进程退出时不会收到SIGCHLD信号，这时子进程如果退出也不会成为僵 尸进程。
    //    SA_NODEFER：使对信号的屏蔽无效，即在信号处理函数执行期间仍能发出这个信号。
    //    SA_RESETHAND：信号处理之后重新设置为默认的处理方式。
    //    SA_SIGINFO：使用sa_sigaction成员而不是sa_handler作为信号处理函数。
    handler.sa_flags = SA_SIGINFO|SA_ONSTACK;

    // 注册信号处理函数
    // 参1  代表信号编码，可以是除SIGKILL及SIGSTOP外的任何一个特定有效的信号，如果为这两个信号定义自己的处理函数，将导致信号安装错误。
    // 参2  指向结构体sigaction的一个实例的指针，该实例指定了对特定信号的处理，如果设置为空，进程会执行默认处理。
    // 参3  和参数act类似，只不过保存的是原来对相应信号的处理，也可设置为NULL。
    #define CATCHSIG(X) sigaction(X, &handler, &old_sa[X])

    for(int i=0;i<signal_num;++i){
        CATCHSIG(handle_signals[i]);
    }
    LOGW("NativeCrashHandler has been registered.");

    return JNI_VERSION_1_6;
}

void fpa_error(int i){
    int x = i/0;
}

void assert_error(){
    bool k = false;
    assert(k);
}

void seg_error(int i){
        char *ptr = NULL;    // 赋值为NULL,空指针,值为0
        *(ptr+i) = '!'; // ERROR HERE! 
}

    JNIEXPORT jstring JNICALL
    Java_monitor_NativeCrash_NativeTest_nativeMakeError(JNIEnv *env, jobject instance) {
        // 故意制造一个信号11异常
        int i=0;
        i +=0;
        i +=0;
//        SIGILL,     // 信号4   非法指令
//                SIGABRT,    // 信号6   来自abort函数的终止信号
//                SIGBUS,     // 信号7   总线错误
//                SIGFPE,    // 信号8   浮点异常
//                SIGSEGV,    // 信号11   无效的存储器引用(段故障)
//#ifdef SIGSTKFLT
//                SIGSTKFLT,// 信号16 协处理器上的栈故障
//#endif
//                SIGPIPE};
//        assert_error();
//        fpa_error(10);
        seg_error(2);
        return env->NewStringUTF("");
    }


}
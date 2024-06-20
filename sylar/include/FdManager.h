#include "FdCtx.h"
#include <vector>

class FdManager{
public:
    typedef RWMutex RWMutexType;
    // 无参构造函数
    FdManager();

    /**
     * @brief 获取/创建文件句柄类FdCtx
     * @param[in] fd 文件句柄
     * @param[in] auto_create 是否自动创建
     * @return 返回对应文件句柄类FdCtx::ptr
     */
    FdCtx::ptr get(int fd, bool auto_create = false);

    /**
     * @brief 删除文件句柄类
     * @param[in] fd 文件句柄
     */
    void del(int fd);

private:
    // 读写锁
    RWMutexType m_mutex;
    // 文件句柄集合
    std::vector<FdCtx::ptr> m_datas;
    };


/// ⽂件句柄单例
typedef Singleton<FdManager> FdMgr;
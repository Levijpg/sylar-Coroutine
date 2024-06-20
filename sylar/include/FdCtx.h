#include <memory>

/**
 * @brief 文件句柄上下文类
 * @details 管理文件句柄类型（是否Socket）
 * 是否阻塞，是否关闭，读写超时时间
 */
class FdCtx : public std::enable_shared_from_this<FdCtx>{
public:
    typedef std::shared_ptr<FdCtx> ptr;
    /**
     * @brief 通过文件句柄构造FdCtx
     */
    FdCtx(int fd);
    /**
     * @brief 析构函数
     */
    ~FdCtx();

    /**
     * @brief 是否初始化完成
     */
    bool isInit() const { return m_isInit;}

    /**
     * @brief 是否socket
     */
    bool isSocket() const { return m_isSocket;}

    /**
     * @brief 是否已关闭
     */
    bool isClose() const { return m_isClosed;}

    /**
     * @brief 设置用户主动设置非阻塞
     * @param[in] v 是否阻塞
     */
    void setUserNonblock(bool v) { m_userNonblock = v;}

    /**
     * @brief 获取是否用户主动设置的非阻塞
     */
    bool getUserNonblock() const { return m_userNonblock;}

    /**
     * @brief 设置系统非阻塞
     * @param[in] v 是否阻塞
     */
    void setSysNonblock(bool v) { m_sysNonblock = v;}

    /**
     * @brief 获取系统非阻塞
     */
    bool getSysNonblock() const { return m_sysNonblock;}

    /**
     * @brief 设置超时时间
     * @param[in] type 类型SO_RCVTIMEO(读超时), SO_SNDTIMEO(写超时)
     * @param[in] v 时间毫秒
     */
    void setTimeout(int type, uint64_t v);

    /**
     * @brief 获取超时时间
     * @param[in] type 类型SO_RCVTIMEO(读超时), SO_SNDTIMEO(写超时)
     * @return 超时时间毫秒
     */
    uint64_t getTimeout(int type);

private:
    // 是否初始化
    bool m_isInit = 1;
    // 是否socket
    bool m_isSocket = 1;
    // 是否hook非阻塞
    bool m_sysNonblock = 1;
    // 是否用户主动设置非阻塞
    bool m_userNonblock = 1;
    // 是否关闭
    bool m_isClosed = 1;
    // 文件句柄
    int m_fd;
    // 读超时时间毫秒
    uint64_t m_recvTimeout;
    // 写超时时间毫秒
    uint64_t m_sendTimeout;
};
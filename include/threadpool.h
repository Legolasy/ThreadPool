#ifndef THREADPOOL_H
#define THREADPOOL_H
#include<vector>
#include<thread>
#include <queue>
#include<memory>
#include<atomic>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<unordered_map>
// virtual 不能跟 template T （虚函数表要确定函数类型）
// 实现上帝类，借助基类指针能指向派生类的特性
// 实现接受任意类型的 Any上帝类
class Any
{
public:
    Any() = default;
    ~Any() = default;
    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;
    Any(Any&&) = default;
    Any& operator=(Any&&) = default;
    template<typename T>
    Any(T data):base_(std::make_unique<Derive<T>>(data)){};
    template<typename T>
    T cast_()
    {
        Derive<T>*pd = dynamic_cast<Derive<T>*>(base_.get()); //RTTI
        if(pd == nullptr)
        {
            //转换失败
            throw "Type Unmatch!";
        }
        return pd->data_;
    }
private:
    //基类类型
    class Base
    {
        public:
            virtual ~Base() = default;
    };
    template<typename T>
    class Derive : public Base{
        public:
            Derive(T data):data_(data){};
            T data_;
    };
    //派生类型 模版
    std::unique_ptr<Base>base_;
};

//信号量semaphore 实现 基于 互斥锁+条件变量
class Semaphore
{
public:
    Semaphore(int limit = 0)
        :resLimit_(limit)
        ,isExited_(false)
        {}
    //~Semaphore() = default;
    ~Semaphore()
    {
        isExited_ = true;
        // mtx_.unlock();
        // mtx_.~mutex();
        // cv_.~condition_variable();
    }
    //消耗一个资源
    void wait()
    {
        if(isExited_)return;
        //上锁
        std::unique_lock<std::mutex>lock(mtx_);
        //资源为0 阻塞
        cv_.wait(lock,[&]()->bool {
            return resLimit_>0;
        });
        //消耗资源
        resLimit_--;
    }
    //生产一个资源
    void post()
    {
        if(isExited_)return;
        //上锁
        std::unique_lock<std::mutex>lock(mtx_);
        //生产资源
        resLimit_++;
        //通知消费者
        cv_.notify_all();
    }
private:

    std::atomic_bool isExited_; // 判断当前semaphore是否析构
    std::condition_variable cv_;
    int resLimit_;
    std::mutex mtx_;
};

class Task;
//Result类 获取线程池task返回的结果
class Result
{
public:
    Result(std::shared_ptr<Task> task, bool isValid = true);
    ~Result() = default;
    // 获取任务执行完的返回值
    Any get();
    //
    void setVal(Any any);
private:
    
    Any data_; //存储返回值
    std::shared_ptr<Task>task_; //获取任务对象，防止task完成后析构掉
    std::atomic_bool isValid_; //返回值有效flag
    Semaphore sem_; // 信号量 线程没完成task，要阻塞get
};


//线程池 支持 fixed 和 cached 两种模式
enum PoolMode
{
    MODE_FIXED, // 线程数量固定
    MODE_CACHED, // 线程数量可动态增长
};

//任务类型 抽象基类
class Task
{
public:
    Task():rs_(nullptr){};
    ~Task() = default;
    // 用户可重写run，提交自定义任何类型的任务
    virtual Any run() = 0;
    // 封装exec 运行+保存结果进Result
    void exec();
    // 设置 rs
    void setResult(Result*rs);
private:
    Result *rs_;
};


//线程类型
class Thread
{
public:
    using ThreadFunc = std::function<void(int)>;
    //构造函数
    Thread(ThreadFunc func);
    //析构函数
    ~Thread();
    // 启动线程
    void start();
    // 获取线程id
    int getID() const;
private:
    ThreadFunc func_;
    static int generate_id;
    int threadId_; //线程id 用来映射 删除vector中哪个thread
};

//线程池类型
class ThreadPool
{
public:
    //线程池构造函数
    ThreadPool();
    //线程池析构函数
    ~ThreadPool();
    // 判断是否运行
    bool checkPoolRunning() const;
    // 设置模式
    void setMode(PoolMode mode);
    // 开启线程池
    void start(int initThreadSize = std::thread::hardware_concurrency());
    // 设置taskQueue 任务上限
    void setTaskQueMaxSize(int threshhold);
    // 设置线程上限
    void setThreadMaxSize(int threshhold);
    // 提交任务
    Result submit(std::shared_ptr<Task>sp);
    // // 设置初始线程数
    // void setInitThreadSize(int size);
    // 禁止拷贝构造、赋值构造
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
private:
    std::unordered_map<int,std::unique_ptr<Thread>>_threads; //线程map
    //std::vector<std::unique_ptr<Thread>> _threads; // 线程列表 智能指针管理内存
    size_t _initThreadSize; // 初始线程数量
    int _maxThreadSize; // 线程最大上限 适用于cached 模式
    std::atomic_int curThreadSize_; //当前总线程数量
    std::atomic_int idleThreadSize_; // 工作线程数量

    std::queue<std::shared_ptr<Task>> _taskQueue; // 任务队列 
    std::atomic_int _taskSize; // 队列任务数量 
    int _maxTaskSize; // 任务最大上限
    std::mutex _taskQueMtx; // 互斥访问任务队列
    std::condition_variable _notFull; // 表示任务队列不满
    std::condition_variable _notEmpty; // 表示任务队列不空
    std::condition_variable _exitCond; // 等待线程资源全部回收

    PoolMode _nowMode; // 当前线程池工作模式
    std::atomic_bool isPoolRunning_; // 线程池运行状态
    
private:
    void threadFunc(int threadID);
};

#endif
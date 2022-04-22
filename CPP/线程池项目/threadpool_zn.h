#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <functional>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include<condition_variable>
#include <unordered_map>
// �̳߳�֧�ֵ�ģʽ
enum class PoolMode //ע������enum��enum class
{
	MODE_FIXED,  // �̶��������߳�
	MODE_CACHED, // �߳������ɶ�̬����
};

// Any���ͣ����Խ����������ݵ�����
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete; //nique_ptr��ֹ����
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	// ������캯��������Any���ͽ�����������������
	template<typename T>  // T:int    Derive<int>
	Any(T data) : base_(std::make_unique<Derive<T>>(data)) //����ָ��ָ�����������
	{}
	//Any(T data) : base_(new Derive<T>(data))
	//{}
	 //��������ܰ�Any��������洢��data������ȡ����
	template<typename T>
	T cast_()
	{
		// ������ô��base_�ҵ�����ָ���Derive���󣬴�������ȡ��data��Ա����
		// ����ָ�� =�� ������ָ��   RTTI
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr)
		{
			throw "type is unmatch!";
		}
		return pd->data_;
	}
private:
	// ��������
	class Base
	{
	public:
		virtual ~Base() = default;
	};

	// ����������
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data) : data_(data)
		{}
		T data_;  // �������������������
	};

private:
	// ����һ�������ָ��
	std::unique_ptr<Base> base_;
};


// ʵ��һ���ź�����
class Semaphore
{
public:
	Semaphore(int limit = 0)
		:resLimit_(limit)
	{}
	~Semaphore() = default;

	// ��ȡһ���ź�����Դ
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		// �ȴ��ź�������Դ��û����Դ�Ļ�����������ǰ�߳�
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--; //����һ���ź�����Դ
	}

	// ����һ���ź�����Դ
	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		// linux��condition_variable����������ʲôҲû��
		// ��������״̬�Ѿ�ʧЧ���޹�����
		cond_.notify_all();  // �ȴ�״̬���ͷ�mutex�� ֪ͨ��������wait�ĵط������������ɻ���
	}
private:
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};



// Task���͵�ǰ������
class Task;

// ʵ�ֽ����ύ���̳߳ص�task����ִ����ɺ�ķ���ֵ����Result
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	// ����һ��setVal��������ȡ����ִ����ķ���ֵ��
	void setVal(Any any);

	// �������get�������û��������������ȡtask�ķ���ֵ
	Any get();
private:
	Any any_; // �洢����ķ���ֵ,��ThreadPool��Result������
	Semaphore sem_; // �߳�ͨ���ź���
	std::shared_ptr<Task> task_; //ָ���Ӧ��ȡ����ֵ��������� 
	std::atomic_bool isValid_; // ����ֵ�Ƿ���Ч
};


// ����������
//�û������Զ��������������� ��Task�̳ж�������дrun������ʵ���Զ���������
class Task
{
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);

	// �û������Զ��������������ͣ���Task�̳У���дrun������ʵ���Զ���������
	virtual Any run() = 0; //����ִ���귵��ֵʱAny ˼����ô����Result

private:
	Result* result_; //��Ҫ����������ָ�룬���ܵ��½������ã� Result������������� �� Task��
};

// �߳�����
class Thread
{
public:
	// �̺߳�����������
	using ThreadFunc = std::function<void(int)>;

	// �̹߳���
	Thread(ThreadFunc func);
	// �߳�����
	~Thread();
	// ������������ÿ���̵߳�
	void start();

	// ��ȡ�߳�id
	int getId()const;
private:
	ThreadFunc func_;//�̺߳������� ThreadPool�е��̺߳���
	static int generateId_;
	int threadId_;  // �����߳�id
};

/*
example:
ThreadPool pool;
pool.start(4);

class MyTask : public Task
{
	public:
		void run() { // �̴߳���... }
};

pool.submitTask(std::make_shared<MyTask>());
*/
// �̳߳�����
class ThreadPool
{
public:
	// �̳߳ع���
	ThreadPool();

	// �̳߳�����
	~ThreadPool();

	// �����̳߳صĹ���ģʽ
	void setMode(PoolMode mode);

	// ����task�������������ֵ
	void setTaskQueMaxThreshHold(int threshhold);

	// �����̳߳�cachedģʽ���߳���ֵ
	void setThreadSizeThreshHold(int threshhold);

	// ���̳߳��ύ����
	Result submitTask(std::shared_ptr<Task> sp);

	// �����̳߳�
	void start(int initThreadSize = 4);

	ThreadPool(const ThreadPool&) = delete; //�����û���ô�� ��Ҫ�þ�ֱ�Ӷ��岻�ÿ���
	ThreadPool& operator=(const ThreadPool&) = delete;//�����û���ô��

private:
	// �����̺߳��� ���ڸ�Thread�������  ִ���̳߳��ύ������
	void threadFunc(int threadid);

	// ���pool������״̬
	bool checkRunningState() const;

private:
	//std::vector<Thread*> threads_; //�߳��б�
	//�Ż���
	//std::vector<std::unique_ptr<Thread>> threads_;//���� vector����ʱ���Զ�����Thread
	//���Ż�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // �߳��б�

	int initThreadSize_; //��ʼ���߳�����

	int threadSizeThreshHold_; // �߳�����������ֵ
	std::atomic_int curThreadSize_;	// ��¼��ǰ�̳߳������̵߳�������
	std::atomic_int idleThreadSize_; // ��¼�����̵߳�����
	std::queue <std::shared_ptr<Task>> tskQue_;//�������
	std::atomic_int taskSize_;//��������
	int taskQueMaxThreshHold_;//�������������ֵ
	std::mutex taskQueMtx_;//��֤������е��̰߳�ȫ 

	std::condition_variable notFull_;  //������в���
	std::condition_variable notEmpty_; //������в���
	std::condition_variable exitCond_; // �ȵ��߳���Դȫ������

	PoolMode poolMode_; // ��ǰ�̳߳صĹ���ģʽ
	std::atomic_bool isPoolRunning_; // ԭ�����ͱ�ʾ��ǰ�̳߳ص�����״̬
};



#endif
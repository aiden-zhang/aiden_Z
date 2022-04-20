
#include "threadpool_zn.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHHOLD = 4;// INT32_MAX; //��4�����Ƿ���ύʧ��
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60; // ��λ����

// �̳߳ع���
ThreadPool::ThreadPool()
	: initThreadSize_(0) //startʱ�ḳֵ
	, taskSize_(0)
	, idleThreadSize_(0)
	, curThreadSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{}

// �̳߳�����
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;

/*
����ʱ�����е�notEmpty_.notify_all()ʱ
threadFunc����������״̬��
1.������notEmpty_.wait(lock)�ϣ���ʱ��notEmpty_.notify_all���ѣ�����isPoolRunning_�����㣬�Լ������߳��˳�
2.�ߵ�while (isPoolRunning_)�е��°벿�֣�����ִ�����񣬵�ִ���������ͷ�ж�while (isPoolRunning_)�������ˣ�Ҳ�ܳɹ����ٲ��˳��˳�
3.����while (isPoolRunning_)������û���е�notEmpty_.wait(lock)�������е�ʱ�Ѿ����������ʱ��notEmpty_.notify_all()��������һ�����߳�notEmpty_.wait(lock)��Զ���ᱻ���ѡ�
���ڵ�������������Ǳ��Ȼ�ȡlock����������notEmpty_.wait(lock)�ϡ�
���������
�޸��������룬����lock��notify�����ڲ�ѭ��˫���жϣ����ڲ�ѭ���˳��������ж�isPoolRunning_�������������̲߳��˳��Ĵ��롣
����ʱ��lock��ȷ��threadFunc��ʹ����while (isPoolRunning_)Ҳ�ò������������ǵ������������ͷ�����threadFunc�õ��������ж�isPoolRunning_�������˳��ڲ�ѭ���������������̲߳��˳�
*/


	////������notify������ͻ���������lock������ܽ���������⣡����
	//֪ͨthreadFunc����notEmpty_��wait��,��ʱ�е��߳���ִ�������е���˯�ߵȴ�������ҪexitCond_.wait�ȴ�ִ������Ļ�˯�ߵĵ������󶼰��߳����ٺ��������
	//notEmpty_.notify_all();

	//// �ȴ��̳߳��������е��̷߳���  ������״̬������ & ����ִ��������
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	
	notEmpty_.notify_all();//������Ų�����������lock��notify�Ͳ�������Ϊʲô�� //�������֮�޸Ģ�

	//����һ��Ҫ����֪ͨ��bingqie�������������Żỽ�ѣ���Ȼ��һֱ��������
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });//ÿ���߳̽��ʱ���ᷢ�źţ���ֻ�е�hreads_.size() == 0������
	std::cout << "now deconstruct ThreadPool!" << std::endl;
}

// �����̳߳صĹ���ģʽ
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}
//void ThreadPool::setInitThredSize(int size)
//{
//	initThreadSize_ = size;
//}

// ����task�������������ֵ
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	//if (checkRunningState())
	//	return;
	taskQueMaxThreshHold_ = threshhold;
}

// �����̳߳�cachedģʽ���߳���ֵ
void ThreadPool::setThreadSizeThreshHold(int threshhold)
{
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThreshHold_ = threshhold;
	}
}


//�û����̳߳��ύ����  �û����øýӿڣ��������������������-->������
Result  ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// ��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	// �̵߳�ͨ��  �ȴ���������п���   wait   wait_for   wait_until
	// �û��ύ�����������������1s�������ж��ύ����ʧ�ܣ�����
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),[&]()->bool { return tskQue_.size() < (size_t)taskQueMaxThreshHold_; }))//wait_for ��1sδ��������������false
	{
		// ��ʾnotFull_�ȴ�1s�֣�������Ȼû������
		std::cerr << "task queue is full, submit task fail." << std::endl;
		// return task->getResult();  //Ϊʲô����  Task  Result   �߳�ִ����task����pop��task����ͱ���������
		return Result(sp, false);
	}

	// ����п��࣬������������������
	tskQue_.emplace(sp);
	taskSize_++;

	// ��Ϊ�·�������������п϶������ˣ���notEmpty_�Ͻ���֪ͨ���Ͽ�����߳�ִ������
	notEmpty_.notify_all();

	// cachedģʽ ������ȽϽ��� ������С��������� ��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳���
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_ //�����߳����Ѿ�����ִ����ô������
		&& curThreadSize_ < threadSizeThreshHold_) //���ܳ�������߳�����
	{
		std::cout << ">>> create new thread..." << std::endl;

		// �����µ��̶߳��󣬰�ThreadPool�еĺ�������󶨵�Thread�ϵ�func_
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));//�����߳��б�

		// �����������߳�
		threads_[threadId]->start();
		// �޸��̸߳�����صı���
		curThreadSize_++;//������map������size��ʾ
		idleThreadSize_++;
	}

	// ���������Result����
	return Result(sp);


	// return task->getResult();
}


// �����̳߳�
void ThreadPool::start(int initThreadSize)
{
	// ���������̳߳ص�����״̬
	isPoolRunning_ = true;

	// ��¼��ʼ�̸߳���
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// �����̶߳���
	for (int i = 0; i < initThreadSize_; i++)
	{
		// ����thread�̶߳����ʱ�򣬰��̺߳�������thread�̶߳���
		//auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));

		//threads_.emplace_back(ptr);//unique_ptr������ֱ�ӿ�������
		 //threads_.emplace(std::move(ptr));
	}

	// ���������߳�  std::vector<Thread*> threads_;
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start(); // ��Ҫȥִ�д���һ���̣߳���ִ���̺߳���
		idleThreadSize_++;    // ��¼��ʼ�����̵߳�����
	}
}

//�����̺߳���  �̳߳ص������̴߳��������������������
void ThreadPool::threadFunc(int threadid)// �̺߳������أ���Ӧ���߳�Ҳ�ͽ�����//��ʼ4���̶߳�ִ�������������task����ȡ����ִ��
{
	//std::cout << "begin threadFunc tid:" <<std::this_thread::get_id()
	//	<< std::endl;

	//std::cout << "end threadFunc tid:" << std::this_thread::get_id()
	//	<< std::endl;
	auto lastTime = std::chrono::high_resolution_clock().now();

	while (isPoolRunning_)
	{
		std::shared_ptr<Task> task;//��ָ��
		
		{//�����޶�������
			//��ȡ��-->��֤ȡtask���а�ȫִ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid:" << std::this_thread::get_id()
				<< "���Ի�ȡ����..." << std::endl;


			// cachedģʽ�£��п����Ѿ������˺ܶ���̣߳����ǿ���ʱ�䳬��60s��Ӧ�ðѶ�����߳�
			// �������յ�������initThreadSize_�������߳�Ҫ���л��գ�
			// ��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� > 60s

			// ÿһ���з���һ��   ��ô���֣���ʱ���أ������������ִ�з���
			// �� + ˫���ж�
			while (tskQue_.size() == 0 && isPoolRunning_) //�������֮�޸Ģ�:˫���ж�
			{
				// �̳߳�Ҫ�����������߳���Դ
				//if (!isPoolRunning_)
				//{
				//	threads_.erase(threadid); // std::this_thread::getid()
				//	std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
				//		<< std::endl;
				//	exitCond_.notify_all();
				//	return; // �̺߳����������߳̽���
				//}

				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					// ������������ʱ������
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_)
						{
							// ��ʼ���յ�ǰ�߳�
							// ��¼�߳���������ر�����ֵ�޸�
							// ���̶߳�����߳��б�������ɾ��   û�а취 threadFunc��=��thread����
							// threadid => thread���� => ɾ��
							threads_.erase(threadid); // ȴ����c++�ɵ�id std::this_thread::getid()
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
								<< std::endl;
							return;
						}
					}
				}
				else //MODE_FIXED
				{
					// �ȴ�notEmpty����  û������͵�
					std::cout << "------------------ready to wait on notEmpty!" << std::endl;
					notEmpty_.wait(lock);
					std::cout << "------------------ unlock on notEmpty!" << "threads_.size() =" << threads_.size()<<std::endl;
				}
				//��������ģʽ�����Ѻ���Ҫô�̳߳�Ҫ��������Ҫ�����߳���Դ��Ҫô������Ҫִ�У�����while(tskQue_.size() == 0)
				if (!isPoolRunning_)
				{
					break;
				}
			} //end of while (tskQue_.size() == 0)


			if (!isPoolRunning_) //�������֮�޸Ģ�:������isPoolRunning_ʱҪ��ʱ�����̣߳�ͬʱ����while����Ըĳ�ֱ��break
			{
				threads_.erase(threadid); // std::this_thread::getid()
				std::cout << "threadid:" << std::this_thread::get_id() << " will exit!"
					<< std::endl;
				exitCond_.notify_all();//֪ͨThreadPool������������
				return; // �����̺߳��������ǽ�����ǰ�߳���!
			}

			//�������������while��������˳�while
			idleThreadSize_--;//������Ͳ�������

			std::cout << "tid:" << std::this_thread::get_id()
				<< "��ȡ����¹�..." << std::endl;

			//���������ȡһ���������
			task = tskQue_.front();
			tskQue_.pop();
			taskSize_--;
			if( tskQue_.size() > 0 )
			{
				notEmpty_.notify_all();
			}
			
			//ȡ������Ҫ֪ͨ,֪ͨ���Լ����ύ��������
			notFull_.notify_all();
		}//����������� ��Ӧ���ͷ��� 

 		//��ǰ�̸߳���ִ������
		if (task != nullptr)
		{
			std::cout << "tid:" << std::this_thread::get_id()
				<< "run task..." << std::endl;
			// task->run(); // ִ�����񣻰�����ķ���ֵsetVal��������Result
			task->exec();
			
		}

		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now(); // �����߳�ִ���������ʱ��

	} //end of while (isPoolRunning_)
	
	//����while �������߳�
	threads_.erase(threadid); // std::this_thread::getid()
	std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
		<< "threads_.size() ="<< threads_.size()
		<< std::endl;
	exitCond_.notify_all();//ִ�����������֪ͨThreadPool������������

}


bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

////////////////  �̷߳���ʵ��
int Thread::generateId_ = 0;  //��̬������Ҫ�����ʼ��

// �̹߳���
Thread::Thread(ThreadFunc func)
	: func_(func) //���ܰ����󶨵ĺ���
	, threadId_(generateId_++)//���id�û��������ڱ�ʶÿһ��thread
{}

// �߳�����
Thread::~Thread() {}

// �����߳�
void Thread::start()
{
	// ����һ���߳���ִ��һ���̺߳��� pthread_create
	std::thread t(func_, threadId_);  // C++11��˵ �̶߳���t  ���̺߳���func_
	t.detach(); // ���÷����߳� ��Ȼ������ҵ� -->linux�е�  pthread_detach   ��pthread_t���óɷ����߳�
}

int Thread::getId()const
{
	return threadId_;
}

/////////////////  Task����ʵ��
Task::Task()
	: result_(nullptr)
{}

void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run()); // ���﷢����̬����
	}
}

void Task::setResult(Result* res)
{
	result_ = res;
}


/////////////////   Result������ʵ��
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: isValid_(isValid)
	, task_(task)
{
	task_->setResult(this);
}

void Result::setVal(Any any)  // ˭���õ��أ�����
{
	// �洢task�ķ���ֵ
	this->any_ = std::move(any);
	sem_.post(); // �Ѿ���ȡ������ķ���ֵ�������ź�����Դ
}

Any Result::get() // �û����õ�
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait(); // task�������û��ִ���꣬����������û����߳�
	return std::move(any_);//any_�����Ǻ����еľֲ���������Ĭ�ϲ���ƥ�䵽��ֵ
}



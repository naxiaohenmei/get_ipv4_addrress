# get_ipv4_addrress
windows下获取指定网卡的IP地址

#ifndef THREADPOLL_H
#define THREADPOLL_H

#include "thread.h"

#include <atomic>
#include <condition_variable>
#include <future>
#include <functional>
#include <iostream>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

constexpr size_t taskMaxThresHold = 2000;

class ThreadPool
{
public:
	enum class PoolMode
	{
		MODE_FIXED,
		MODE_CACHED
	};

	enum class PoolTaskMode
	{
		CPU_BOUND = 1,
		IO_BOUND,
		CYCLE_IO_BOUND,
	};

	ThreadPool()
	{
		threadsM.reserve(taskMaxThresHold);
	}

	~ThreadPool()
	{
		isPoolRunningM.store(false, std::memory_order_relaxed);

		std::unique_lock<std::mutex> lc(taskQueMtxM);
		notEmptyM.notify_all();
		exitM.wait(lc, [&]()->bool {return threadsM.size() == 0; });
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	// 启动线程池
	void startThreadPool(size_t initThreadSize = 0)
	{
		if (checkRunningState())
			return;

		isPoolRunningM.store(true, std::memory_order_relaxed);

		size_t concurrencyCount = std::thread::hardware_concurrency();

		if (0 == taskQueMaxThresHoldM) {
			taskQueMaxThresHoldM = concurrencyCount * 5;
		}

		if (0 == initThreadSize) {
			initThreadSizeM = concurrencyCount * static_cast<size_t>(poolTaskModeM);
		}
		else {
			initThreadSizeM = initThreadSize;
		}

		for (size_t i = 0; i < initThreadSizeM; i++) {
			std::unique_ptr<Thread> up = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			threadsM.emplace(up->getThreadId(), std::move(up));
		}

		for (const auto& [_, thread] : threadsM) {
			thread->start();
		}
	}

	//设置线程池工作模式
	void setThreadPoolMode(PoolMode mode)
	{
		if (checkRunningState())
			return;

		poolModeM = mode;
	}

	// 设置任务队列的上限阈值
	void setTaskQueMaxThresHold(size_t thresHold)
	{
		if (checkRunningState())
			return;

		taskQueMaxThresHoldM = thresHold;
	}

	// 设置线程池任务模式
	void setPoolTaskMode(PoolTaskMode mode)
	{
		if (checkRunningState())
			return;

		poolTaskModeM = mode;
	}

	// 添加任务
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		if (isPoolpauseM.load(std::memory_order_acquire)) {
			std::unique_lock<std::mutex> lc(poolPauseMtxM);
			pauseM.wait(lc, [&]()-> bool { return !isPoolpauseM.load(std::memory_order_acquire); });
		}

		using RType = decltype(func(args...));

		auto spTask = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
		);
		std::future<RType> res = spTask->get_future();

		std::unique_lock<std::mutex> lc(taskQueMtxM);
		
		if (!notFullM.wait_for(lc, std::chrono::seconds(1), [&]()->bool {return taskQueM.size() < taskQueMaxThresHoldM; })) {
			std::cout << __FILE__ << ":" << __LINE__ << " task queue is full, summit task fail." << std::endl;

			auto spTask = std::make_shared<std::packaged_task<RType()>>(
				[]() -> RType { return RType() ; });

			(*spTask)();
			return spTask->get_future();
		}
		
		//std::cout << "submitTask... " << std::endl;

		taskQueM.emplace([spTask]() {(*spTask)(); });
		taskSizeM.fetch_add(1, std::memory_order_acquire);

		notEmptyM.notify_all();

		return res;
		// cached add
	}

	// 设置线程的上限阈值
	void setThreadSizeMaxThresHold(size_t thresHold)
	{
		if (checkRunningState() && PoolMode::MODE_CACHED != poolModeM)
			return;

		threadSizeMaxThresHoldM = thresHold;
	}

	// 线程池挂起/停止执行
	void suspendThreadPool()
	{
		if (!checkRunningState())
			return;

		isPoolpauseM.store(true, std::memory_order_release);
	}

	// 恢复线程池/继续执行
	void resumeThreadPool()
	{
		if (!checkRunningState())
			return;

		isPoolpauseM.store(false, std::memory_order_acquire);

		pauseM.notify_all();
	}

private:
	// 线程函数
	void threadFunc(size_t threadId)
	{
		//std::cout << "starting... " << threadId << std::endl;

		for (;;) {
			Task spTask;

			{
				std::unique_lock<std::mutex> lc(taskQueMtxM);
				notEmptyM.wait(lc, [&]()-> bool { return taskQueM.size() > 0 || !isPoolRunningM.load(std::memory_order_acquire); });

				if (!isPoolRunningM.load(std::memory_order_acquire) && 0 == taskQueM.size()) {
					auto res = threadsM.erase(threadId);
					exitM.notify_all();
					return;
				}

				// cached sub

				spTask = taskQueM.front();
				taskQueM.pop();
				taskSizeM.fetch_sub(1, std::memory_order_acquire);

				if (0 < taskQueM.size()) {
					notEmptyM.notify_all();
				}

				notFullM.notify_all();
			}

			if (spTask) {
				if (isPoolpauseM.load(std::memory_order_acquire)) {
					std::unique_lock<std::mutex> lc(poolPauseMtxM);
					pauseM.wait(lc, [&]()-> bool { return !isPoolpauseM.load(std::memory_order_acquire); });
				}

				try {
					taskRunSizeM.fetch_add(1, std::memory_order_release);
					spTask();
					if (!isTaskRunningM.load(std::memory_order_acquire)) {
						isTaskRunningM.store(true, std::memory_order_release);
					}
					taskRunSizeM.fetch_sub(1, std::memory_order_acquire);
					allCompletedM.notify_all();
				}
				catch (const std::future_error& e) {
					std::cout << "Caught a future_error with code \"" << e.code()
						<< "\"\nMessage: \"" << e.what() << "\"\n";
				}
			}
		}
	}

	// 检查线程池的运行状态
	bool checkRunningState() const
	{
		return isPoolRunningM.load(std::memory_order_relaxed);
	}

private:
	std::unordered_map<size_t, std::unique_ptr<Thread>> threadsM;
	size_t initThreadSizeM{ 0 }; // 初始化线程个数
	size_t idleThreadSizeM{ 0 }; // 空闲线程数量
	size_t threadSizeMaxThresHoldM{ 0 }; // 线程上限阈值

	using Task = std::function<void()>;
	std::queue<Task> taskQueM;
	std::atomic_uint taskSizeM{ 0 }; // 任务的数量
	size_t taskQueMaxThresHoldM{ 0 }; // 任务队列数量上限阈值

	std::mutex taskQueMtxM; // 保证任务队列的线程安全
	std::mutex poolPauseMtxM; // 保证线程池暂停的线程安全
	std::condition_variable notFullM; // 任务队列不满
	std::condition_variable notEmptyM; // 任务队列不空
	std::condition_variable exitM; // 退出/销毁线程资源
	std::condition_variable pauseM; // 停止线程池
	
	std::atomic_bool isPoolpauseM{ false }; // 线程池停止状态

	PoolMode poolModeM{ PoolMode::MODE_FIXED }; // 线程池模式
	PoolTaskMode poolTaskModeM{ PoolTaskMode::CYCLE_IO_BOUND }; // 线程池任务模式
	std::atomic_bool isPoolRunningM{ false }; // 当前线程池启动状态

	/* ------------------------------------------------------------- */
public:
	void waitAllThreadsCompleteExecution()
	{
		std::unique_lock<std::mutex> lc_(allCompletedMtxM);
		allCompletedM.wait(lc_, [&]()->bool {return taskRunSizeM.load(std::memory_order_seq_cst) == 0 && isTaskRunningM.load(std::memory_order_acquire); });
		isTaskRunningM.store(false, std::memory_order_release);
	}
private:
	std::condition_variable allCompletedM; // 所有子线程任务执行完成
	std::atomic_uint taskRunSizeM{ 0 }; // 任务执行中的数量
	std::mutex allCompletedMtxM; // 
	std::atomic_bool isTaskRunningM{ false };
};

#endif // !THREADPOLL_H


-----------------------------------

#ifndef THREAD_H
#define THREAD_H

#include <functional>

class Thread
{
public:
	using ThreadFunc = std::function<void(size_t)>;

	explicit Thread(ThreadFunc func);
	~Thread();

	Thread(const Thread&) = delete;
	Thread& operator=(const Thread&) = delete;

	// 启动线程
	void start() noexcept;

	// 获取线程ID
	size_t getThreadId();

private:
	ThreadFunc funcM;
	size_t threadIdM;

	inline static size_t generateIdM{ 0 };
};

#endif // !THREAD_H

-----------------------------------------------------

#include "thread.h"

#include <iostream>
#include <thread>

Thread::Thread(ThreadFunc func)
	: funcM(func)
	, threadIdM(generateIdM++)
{
}

Thread::~Thread()
{
	std::cout << "destroy thread: " << threadIdM << std::endl;
}

void Thread::start() noexcept
{
	std::thread t(funcM, threadIdM);
	t.detach();
}

size_t Thread::getThreadId()
{
	return threadIdM;
}

---------------------------------------------------------------------------

#ifndef IPCONFIG_H
#define IPCONFIG_H

#include <string>
#include <vector>
#include <iostream>
#include <unordered_set>

//netsh interface ip delete address "网卡名称" addr=IP地址

class IPConfig
{
public:
    IPConfig();
    ~IPConfig();

    std::vector<std::string>& getIpAddresses();

    int getIpAddrTotal() const;

    bool createIPList(const std::string& startIp, const std::string& endIp, const std::string& serviceIp);

    // 添加IP到网卡
    void addIp();

private:
    // 计算ip地址
    bool init(const std::string& startIp, const std::string& endIp, const std::string& serviceIp);

#ifdef _MSC_VER
    std::string WString2String(const std::wstring& ws);
    void getAdaptersIPAddressesInfoWin();
#elif __linux__
    void getAdaptersIPAddressesInfoLinux();
#endif

private:
    std::vector<std::string> ipAddressesM{};
    std::unordered_set<std::string> realIPaddressesM{};
    int ipAddrTotalM{ 0 };
};

#endif // !IPCONFIG

------------------------------------------------------

#include "ipconfig.h"
#ifdef _MSC_VER
#include <WinSock2.h>
#include <Iphlpapi.h>
#include <ws2tcpip.h>
#elif __linux__
﻿#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#endif

constexpr int IpCount{ 2000 };
constexpr int ThirdSegmentIPMaxValue{ 254 };
constexpr int FourthSegmentIPMaxValue{ 254 };
constexpr int FourthSegmentIPMinValue{ 1 };

constexpr char EthernetAdapter[]{ "Ethernet" };
#ifdef _MSC_VER
constexpr char Command[]{ "netsh interface ip add address" };
constexpr char NetMask[]{ "255.255.255.0" };
#elif __linux__
constexpr char Command[]{ "sudo ip addr add" };
constexpr char NetMask[]{ "/24" }; // 255.255.255.0
constexpr char Device[]{ "dev" };
#endif

std::string operator++(std::string& ip, int) {
	std::string originalIp = ip;

	size_t pos = ip.find('.', ip.find('.', 0) + 1);
	std::string firstTwoSegmentIp = ip.substr(0, pos + 1);
	std::string laterTwoSegmentIp = ip.substr(pos + 1);
	int thirdSegmentIp = std::stoi(laterTwoSegmentIp.substr(0, laterTwoSegmentIp.find('.')));
	int fourthSegmentIp = std::stoi(laterTwoSegmentIp.substr(laterTwoSegmentIp.find('.') + 1));

	if (fourthSegmentIp / FourthSegmentIPMaxValue)
	{
		if (!(thirdSegmentIp / ThirdSegmentIPMaxValue))
		{
			fourthSegmentIp = FourthSegmentIPMinValue;
			++thirdSegmentIp;
		}
	}
	else
	{
		++fourthSegmentIp;
	}

	ip = firstTwoSegmentIp.append(std::to_string(thirdSegmentIp)).append(".").append(std::to_string(fourthSegmentIp));

	return move(originalIp);
}

IPConfig::IPConfig()
{
	ipAddressesM.reserve(IpCount);
	realIPaddressesM.reserve(IpCount * 1.5);

#ifdef _MSC_VER
	getAdaptersIPAddressesInfoWin();
#elif __linux__
#endif
}

IPConfig::~IPConfig()
{
}

bool IPConfig::init(const std::string& startIp, const std::string& endIp, const std::string& serviceIp)
{
	std::string startIpAddr{ startIp };
	std::string endIpAddr{ endIp };
	std::string serviceIpAddr{ serviceIp };

	std::string ip;

	for (;;) {
		ip = startIpAddr++;

		if (serviceIpAddr == ip) {
			if (endIpAddr == ip) {
				break;
			}

			ip = startIpAddr++;
		}
		if (realIPaddressesM.find(ip) == realIPaddressesM.end()) {
			return false;
		}

		ipAddressesM.emplace_back(ip);
		++ipAddrTotalM;

		if (endIpAddr == ip) {
			break;
		}
	}

	return true;
}

void IPConfig::addIp()
{
#if 0
	std::string s;

	for (const auto& ip : ipAddressesM) {
#ifdef _MSC_VER
		s = std::string(Command) + " " + std::string(EthernetAdapter) + " " + std::string(ip) + " " + std::string(NetMask);
#elif __linux__
		s = std::string(Command) + " " + std::string(ip) + std::string(NetMask) + std::string(Device) + " " + std::string(EthernetAdapter);
#endif
		int res = system(s.c_str());
		if (0 != res) {
			std::cout << "Add " << ip << " failed" << std::endl;
		}
	}
#endif
}

int IPConfig::getIpAddrTotal() const
{
	return ipAddrTotalM;
}

std::vector<std::string>& IPConfig::getIpAddresses()
{
	return ipAddressesM;
}

std::string IPConfig::WString2String(const std::wstring& ws)
{
	std::string strLocale = setlocale(LC_ALL, "");
	const wchar_t* wchSrc = ws.c_str();
	size_t nDestSize = wcstombs(NULL, wchSrc, 0) + 1;
	char* chDest = new char[nDestSize];
	memset(chDest, 0, nDestSize);
	wcstombs(chDest, wchSrc, nDestSize);
	std::string strResult = chDest;
	delete[] chDest;
	setlocale(LC_ALL, strLocale.c_str());
	return strResult;
}

bool IPConfig::createIPList(const std::string& startIp, const std::string& endIp, const std::string& serviceIp)
{
	return init(startIp, endIp, serviceIp);
}
#ifdef _MSC_VER
void IPConfig::getAdaptersIPAddressesInfoWin()
{
	ULONG outBufLen = sizeof(IP_ADAPTER_ADDRESSES) * 403;
	PIP_ADAPTER_ADDRESSES pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
	
	if (GetAdaptersAddresses(AF_INET, 0, NULL, pAddresses, &outBufLen) == ERROR_BUFFER_OVERFLOW)
	{
		free(pAddresses);
		pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
	}

	if (GetAdaptersAddresses(AF_INET, 0, NULL, pAddresses, &outBufLen) == NO_ERROR)
	{
		PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses;
		while (pCurrAddresses != NULL)
		{
			std::wstring name = pCurrAddresses->FriendlyName;

			std::string str = WString2String(name);
			if (str == std::string(EthernetAdapter))
			{
				auto ptr = pCurrAddresses->FirstUnicastAddress;
				while (ptr)
				{
					if (ptr->Address.lpSockaddr->sa_family == AF_INET)
					{
						sockaddr_in* sa_in = (sockaddr_in*)ptr->Address.lpSockaddr;
						std::string ipv4Addr = inet_ntoa(sa_in->sin_addr);
						std::cout << "ipv4 " << ipv4Addr << std::endl;
						realIPaddressesM.emplace(ipv4Addr);
					}

					ptr = ptr->Next;
				}
			}

			pCurrAddresses = pCurrAddresses->Next;
		}
	}

	free(pAddresses);
}
#elif __linux__
void IPConfig::getAdaptersIPAddressesInfoLinux()
{
	int fd, intrface, retn = 0;
	struct ifreq buf[INET_ADDRSTRLEN];  //这个结构定义在/usr/include/net/if.h，用来配置和获取ip地址，掩码，MTU等接口信息的
	struct ifconf ifc;
	char ip[64];

	/*1 建立socket链接，利用ioctl来自动扫描可用网卡*/
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0)
	{
		ifc.ifc_len = sizeof(buf);

		// caddr_t,linux内核源码里定义的：typedef void *caddr_t；
		ifc.ifc_buf = (caddr_t)buf;

		if (!ioctl(fd, SIOCGIFCONF, (char*)&ifc))  /*2  这里获取到可用网卡清单，包括网卡ip地址，mac地址*/
		{
			intrface = ifc.ifc_len / sizeof(struct ifreq);  //计算出有效网卡的数量//
			while (intrface-- > 0)
			{
				if (!(ioctl(fd, SIOCGIFADDR, (char*)&buf[intrface])))  /*3  遍历并索引指定网卡的地址*/
				{
					if (strcmp(buf[intrface].ifr_ifrn.ifrn_name, EthernetAdapter) == 0)
					{
						ip = (inet_ntoa(((struct sockaddr_in*)(&buf[intrface].ifr_addr))->sin_addr));
						std::cout << "IP: " << ip << std::endl;

						realIPaddressesM.emplace(std::string(ip));
					}
				}
			}
		}

		close(fd);
	}
}
#endif

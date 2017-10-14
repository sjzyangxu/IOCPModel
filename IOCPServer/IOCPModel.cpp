#include "stdafx.h"
#include "IOCPModel.h"


CIOCPModel::CIOCPModel()
	:m_nPort(DEFAULT_PORT),
	 m_numThreads(0),
	 m_hIOCP(NULL),
	 m_hQuitEvent(NULL),
	 m_phWorkerThreads(nullptr),
	 m_pListenContext(nullptr),
	 m_lpfnAcceptEx(nullptr),
	 m_lpfnGetAcceptExSockAddrs(nullptr)
{
	//��ʼ���̻߳�����
	InitializeCriticalSection(&m_csContextList);
	//�����߳��˳��¼� Ĭ�����ź�  ��Ĭ�������ź�״̬
	m_hQuitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	//���÷�������ַ��Ϣ
	m_serverAddr->sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	m_serverAddr->sin_family = AF_INET;
	m_serverAddr->sin_port = DEFAULT_PORT;
}


CIOCPModel::~CIOCPModel()
{
}

DWORD WINAPI CIOCPModel::WorkerThreadFun(LPVOID lpParam)
{
	return 0;
}

bool CIOCPModel::LoadSocketLab()
{
	WSADATA wsaData;
	//���ִ���
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != NO_ERROR)
	{
		printf("��ʼ��winsock 2.2ʧ��\n");
		return false;
	}
	return true;
}

bool CIOCPModel::Init()
{
	return true;
}



bool CIOCPModel::InitIOCP()
{
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (NULL==m_hIOCP)
	{
		printf("������ɶ˿�ʧ�ܣ�������룺%d\n", WSAGetLastError());
		return false;
	}
	return true;
}

bool CIOCPModel::InitSocket()
{
	// AcceptEx �� GetAcceptExSockaddrs ��GUID�����ڵ�������ָ��
	GUID GuidAcceptEx = WSAID_ACCEPTEX;
	GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;

	//ע�� ��Ҫ��wsasocket����
	m_pListenContext->m_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == m_pListenContext->m_socket)
	{
		printf("��ʼ��socketʧ�ܣ������룺%d\n", WSAGetLastError());
		return false;
	}
	//�󶨵���������ַ
	if (SOCKET_ERROR==bind(m_pListenContext->m_socket, (sockaddr *)&m_serverAddr, sizeof(m_serverAddr)))
	{
		printf("bind()����ִ�д���\n");
		RELEASE_SOCKET(m_pListenContext->m_socket);
		return false;
	}
	//������ɶ˿�
	if (NULL == CreateIoCompletionPort((HANDLE)m_pListenContext->m_socket, m_hIOCP, (DWORD)m_pListenContext, 0))
	{
		printf("��listen socket����ɶ˿�ʧ�ܣ�������룺%d\n", WSAGetLastError());
		RELEASE_SOCKET(m_pListenContext->m_socket);
		return false;
	}
	//��ʼ����
	if (SOCKET_ERROR == listen(m_pListenContext->m_socket, 10))
	{
		printf("listen()����ִ��ʧ�ܣ����ü���ʧ�ܣ�������룺%d\n", WSAGetLastError());
		RELEASE_SOCKET(m_pListenContext->m_socket);
		return false;
	}


	// ʹ��AcceptEx��������Ϊ���������WinSock2�淶֮���΢�������ṩ����չ����
	// ������Ҫ�����ȡһ�º�����ָ�룬
	// ��ȡAcceptEx����ָ��
	DWORD dwBytes = 0;
	if (SOCKET_ERROR == WSAIoctl(
		m_pListenContext->m_socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidAcceptEx,
		sizeof(GuidAcceptEx),
		&m_lpfnAcceptEx,
		sizeof(m_lpfnAcceptEx),
		&dwBytes,
		NULL,
		NULL))
	{
		DeInit();
		return false;
	}

	// ��ȡGetAcceptExSockAddrs����ָ�룬Ҳ��ͬ��
	if (SOCKET_ERROR == WSAIoctl(
		m_pListenContext->m_socket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GuidGetAcceptExSockAddrs,
		sizeof(GuidGetAcceptExSockAddrs),
		&m_lpfnGetAcceptExSockAddrs,
		sizeof(m_lpfnGetAcceptExSockAddrs),
		&dwBytes,
		NULL,
		NULL))
	{
		DeInit();
		return false;
	}

	//Ϊacceptex׼��������Ȼ��Ͷ��io����
	for (int i = 0; i < MAX_POST_ACCEPT; i++)
	{
		//�½�һ��io_context
		PPER_IO_CONTEXT p = m_pListenContext->GetNewIOContext();
		if (false == PostAccept(p))
		{
			m_pListenContext->RemoveContext(p);
			return false;
		}
	}

	return true;
}

bool CIOCPModel::InitWorkerThread()
{
	//��ô���������
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	int numOfProcessors = si.dwNumberOfProcessors;
	m_numThreads = THREAD_PER_PROCESSOR*numOfProcessors;
	//��ʼ���߳�
	m_phWorkerThreads = new HANDLE[numOfProcessors];
	DWORD nWorkerID;
	for (int i = 0; i < m_numThreads; i++)
	{
		PTHREADPARAM_WORKER param = new THREADPARAM_WORKER;
		param->m_IOCPModel = this;
		param->m_noThread = i + 1;
		m_phWorkerThreads[i] = CreateThread(0, 0, WorkerThreadFun, (LPVOID)param, 0, &nWorkerID);
	}
	printf("�����������߳� %d��\n", m_numThreads);


	return true;
}

void CIOCPModel::DeInit()
{
	//ɾ���̻߳�����
	DeleteCriticalSection(&m_csContextList);
	//�ͷ�iocp�˿ھ��
	RELEASE_HANDLE(m_hIOCP);
	//�ر��¼�
	RELEASE_HANDLE(m_hQuitEvent);
	//�ر��ͷ��߳�
	for (int i = 0; i < m_numThreads; i++)
	{
		RELEASE_HANDLE(m_phWorkerThreads[i]);
	}
	//ɾ��
	delete[m_numThreads]m_phWorkerThreads;

	printf("�ͷ���Դ��ϣ�\n");
}

bool CIOCPModel::PostAccept(PPER_IO_CONTEXT p)
{

	return false;
}

bool CIOCPModel::PostRecv(PPER_IO_CONTEXT p)
{
	return false;
}

bool CIOCPModel::PostSend(PPER_IO_CONTEXT p)
{
	return false;
}

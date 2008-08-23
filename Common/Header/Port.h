#if !defined(AFX_PORT_H__695AAC30_F401_4CFF_9BD9_FE62A2A2D0D2__INCLUDED_)
#define AFX_PORT_H__695AAC30_F401_4CFF_9BD9_FE62A2A2D0D2__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


#include <windows.h>

#define NMEA_BUF_SIZE 100

class ComPort {
 public:
	ComPort(int the_devIdx);
	~ComPort() { };

	void PutChar(BYTE);
	void WriteString(const TCHAR *);
	void Flush();

	BOOL Initialize(LPCTSTR, DWORD);
	BOOL Close();

	int SetRxTimeout(int);
	unsigned long SetBaudrate(unsigned long);

	BOOL StopRxThread();
	BOOL StartRxThread();
	void ProcessChar(char);

	int GetChar();
	int Read(void *Buffer, size_t Size);

 private:
	static DWORD WINAPI ThreadProc(LPVOID);
	DWORD ReadThread();

	HANDLE hPort;
	HANDLE hReadThread;
	DWORD dwMask;
	TCHAR sPortName[8];
	BOOL CloseThread;
	BOOL fRxThreadTerminated;

	TCHAR BuildingString[NMEA_BUF_SIZE];
	int bi;
	int devIdx;
};

#endif

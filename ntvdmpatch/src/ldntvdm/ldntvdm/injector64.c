#ifdef _WIN64
#include "ldntvdm.h"

#ifdef TARGET_WINXP
#include "xpwrap.h"
#define WOW64_CS32   0x23
#define WOW64_TLS_CPURESERVED               1   // per-thread data for the CPU simulator
#endif

typedef struct _PEB32
{
	UCHAR				InheritedAddressSpace;				// 0
	UCHAR				ReadImageFileExecOptions;			// 1
	UCHAR				BeingDebugged;					// 2
	BYTE				b003;								// 3
	ULONG				Mutant;						// 4
	ULONG				ImageBaseAddress;				// 8
	ULONG				Ldr;							// C
	ULONG				ProcessParameters;				// 10
	ULONG				SubSystemData;
	ULONG				ProcessHeap;
	ULONG				FastPebLock;
	ULONG				AtlThunkSListPtr;
	ULONG				IFEOKey;
	union
	{
		ULONG CrossProcessFlags;
		struct
		{
			ULONG ProcessInJob : 1;
			ULONG ProcessInitializing : 1;
			ULONG ProcessUsingVEH : 1;
			ULONG ProcessUsingVCH : 1;
			ULONG ReservedBits0 : 28;
		};
	};
} PEB32, *PPEB32;

typedef struct _UNICODE_STRING32 {
	USHORT Length;
	USHORT MaximumLength;
	ULONG  Buffer;
} UNICODE_STRING32;

typedef struct _LDR_DATA_TABLE_ENTRY32
{
	LIST_ENTRY32 InLoadOrderModuleList;
	LIST_ENTRY32 InMemoryOrderModuleList;
	LIST_ENTRY32 InInitializationOrderModuleList;
	ULONG DllBase;
	ULONG EntryPoint;
	ULONG SizeOfImage;
	UNICODE_STRING32 FullDllName;
	UNICODE_STRING32 BaseDllName;
	ULONG Flags;
	USHORT LoadCount;
	USHORT TlsIndex;
	union
	{
		LIST_ENTRY32 HashLinks;
		PVOID SectionPointer;
	};
	ULONG CheckSum;
	union
	{
		ULONG TimeDateStamp;
		PVOID LoadedImports;
	};
	ULONG EntryPointActivationContext;
	ULONG PatchInformation;
} LDR_DATA_TABLE_ENTRY32, *PLDR_DATA_TABLE_ENTRY32;

typedef struct _PEB_LDR_DATA32
{
	ULONG Length;
	BOOLEAN Initialized;
	ULONG SsHandle;
	LIST_ENTRY32 InLoadOrderModuleList;
	LIST_ENTRY32 InMemoryOrderModuleList;
	LIST_ENTRY32 InInitializationOrderModuleList;
} PEB_LDR_DATA32, *PPEB_LDR_DATA32;


typedef struct
{
	HANDLE hFile;
	HANDLE hMap;
	PBYTE lpMem;
} HMODULE32;

BOOL isProcessInitialized(HANDLE hProcess)
{
#ifdef TARGET_WINXP
	/* On Windows XP, we get called at ConnectConsoleInternal, Hijacking would be
	* possible, but it's too early, not all imports may have been bound at the time
	* when the console client library is loaded.
	*/
	return FALSE;
#else
	PEB32 *pWow64PEB, Wow64PEB;
	NTSTATUS Status;

	/* query the process basic information (includes the PEB address) */
	if (!NT_SUCCESS(Status = NtQueryInformationProcess(hProcess, ProcessWow64Information, &pWow64PEB,
		sizeof(pWow64PEB), NULL)) ||
		/* get the address of the PE Loader data */
		!ReadProcessMemory(hProcess, pWow64PEB, &Wow64PEB, sizeof(Wow64PEB), NULL))
		return TRUE;	// Better lie...
	TRACE("ProcessInitializing = %d\n", Wow64PEB.ProcessInitializing);
	return !Wow64PEB.ProcessInitializing;
#endif
}

void FreeLibrary32(HANDLE hModule)
{
	HMODULE32 *hMod = (HMODULE32*)hModule;

	UnmapViewOfFile(hMod->lpMem);
	CloseHandle(hMod->hMap);
	CloseHandle(hMod->hFile);
	HeapFree(GetProcessHeap(), 0, hMod);
}

HANDLE LoadLibrary32(WCHAR *pszFile)
{
	HMODULE32 *hMod = HeapAlloc(GetProcessHeap(), 0, sizeof(HMODULE32));

	hMod->hFile = CreateFile(pszFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

	if (hMod->hFile != INVALID_HANDLE_VALUE)
	{
		if (hMod->hMap = CreateFileMapping(hMod->hFile, NULL, PAGE_READONLY, 0, 0, NULL))
		{
			if (hMod->lpMem = MapViewOfFile(hMod->hMap, FILE_MAP_READ, 0, 0, 0))
			{
				return hMod;
			}
			CloseHandle(hMod->hMap);
		}
		CloseHandle(hMod->hFile);
	}
	HeapFree(GetProcessHeap(), 0, hMod);
	return NULL;
}

static DWORD GetProcAddress32(HANDLE hModule, char *lpProcName)
{
	PBYTE pBase = ((HMODULE32*)hModule)->lpMem;
	PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)pBase;
	PIMAGE_NT_HEADERS32 NtHeaders = (PIMAGE_NT_HEADERS32)(pBase + DosHeader->e_lfanew);
	PIMAGE_EXPORT_DIRECTORY pExportDir = NULL;
	PIMAGE_SECTION_HEADER SectHdr = (PIMAGE_SECTION_HEADER)((PBYTE)NtHeaders + sizeof(IMAGE_NT_HEADERS32));
	DWORD EATAddr = NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
	PDWORD pNamesArray, pAddressArray;
	PWORD pNameOrdinalsArray;
	DWORD i, j;

	for (i = 0; i < NtHeaders->FileHeader.NumberOfSections; i++)
	{
		if (EATAddr > SectHdr[i].VirtualAddress && EATAddr <= SectHdr[i].VirtualAddress + SectHdr[i].Misc.VirtualSize)
		{
			pExportDir = (PIMAGE_EXPORT_DIRECTORY)(EATAddr - SectHdr[i].VirtualAddress + SectHdr[i].PointerToRawData + pBase);
			pNamesArray = (PDWORD)(pExportDir->AddressOfNames - SectHdr[i].VirtualAddress + SectHdr[i].PointerToRawData + pBase);
			pNameOrdinalsArray = (PWORD)(pExportDir->AddressOfNameOrdinals - SectHdr[i].VirtualAddress + SectHdr[i].PointerToRawData + pBase);
			pAddressArray = (PDWORD)(pExportDir->AddressOfFunctions - SectHdr[i].VirtualAddress + SectHdr[i].PointerToRawData + pBase);
			break;
		}
	}
	if (pExportDir)
	{
		// Search for name of function in the NamesArray   
		for (j = 0; j < pExportDir->NumberOfNames; j++)
		{
			char *pszProc = (char *)(pBase + pNamesArray[j] - SectHdr[i].VirtualAddress + SectHdr[i].PointerToRawData);

			if (!_strcmp(pszProc, lpProcName))
				return pAddressArray[pNameOrdinalsArray[j]];
		}
	}
	return 0;
}

HANDLE GetRemoteModuleHandle32(HANDLE ProcessHandle, LPWSTR lpDllName)
{
	PEB32 *pWow64PEB, Wow64PEB;
	NTSTATUS Status;
	PEB_LDR_DATA32 *pMod, *pStart, LoaderData;
	LDR_DATA_TABLE_ENTRY32 ldrMod;
	WCHAR dllName[MAX_PATH + 1];

	/* query the process basic information (includes the PEB address) */
	if (!NT_SUCCESS(Status = NtQueryInformationProcess(ProcessHandle, ProcessWow64Information, &pWow64PEB,
		sizeof(pWow64PEB), NULL)) ||
		/* get the address of the PE Loader data */
		!ReadProcessMemory(ProcessHandle, (LPCVOID)pWow64PEB, &Wow64PEB, sizeof(Wow64PEB), NULL) ||
		!ReadProcessMemory(ProcessHandle, (LPCVOID)Wow64PEB.Ldr, &LoaderData, sizeof(LoaderData), NULL))
		return NULL;

	/* head of the module list: the last element in the list will point to this */
	pStart = pMod = (PEB_LDR_DATA32*)LoaderData.InLoadOrderModuleList.Flink;
	do
	{
		if (!ReadProcessMemory(ProcessHandle, pMod, &ldrMod, sizeof(ldrMod), NULL)) break;
		if (ldrMod.DllBase &&
			ReadProcessMemory(ProcessHandle, (LPCVOID)ldrMod.BaseDllName.Buffer, dllName, min(ldrMod.BaseDllName.Length, sizeof(dllName) / sizeof(WCHAR)), NULL))
		{
			dllName[ldrMod.BaseDllName.Length / sizeof(WCHAR)] = 0;
			if (!__wcsicmp(dllName, lpDllName))
				return (HANDLE)ldrMod.DllBase;
		}
		pMod = (PEB_LDR_DATA32*)ldrMod.InLoadOrderModuleList.Flink;
	} while (pMod != pStart);
	return NULL;
}

DWORD GetRemoteProcAddressX32(HANDLE ProcessHandle, LPWSTR lpDllName, LPSTR lpProcName)
{
	HANDLE hRemoteMod = GetRemoteModuleHandle32(ProcessHandle, lpDllName);
	HANDLE hLibKernel32;
	WCHAR dllName[MAX_PATH + 1];

	if (!hRemoteMod) return 0;
	GetSystemWow64Directory(dllName, sizeof(dllName) / sizeof(WCHAR));
	wcscat(dllName, L"\\");
	wcscat(dllName, lpDllName);
	if (hLibKernel32 = LoadLibrary32(dllName))
	{
		DWORD dwRet = 0;

		dwRet = GetProcAddress32(hLibKernel32, lpProcName);
		FreeLibrary32(hLibKernel32);
		return dwRet ? dwRet + (DWORD)hRemoteMod : 0;
	}
	return 0;
}

DWORD GetLoadLibraryAddressX32(HANDLE ProcessHandle)
{
	return GetRemoteProcAddressX32(ProcessHandle, L"kernel32.dll", "LoadLibraryW");
}

BOOL InjectDllHijackThreadX32(HANDLE hProc, HANDLE hThread, WCHAR *DllName)
{
	//get the thread context
	BYTE code[] = {
		0x60,	// PUSHAD
		0x9C,	// PUSHFD
		0xb8, 0xAA, 0xAA, 0xAA, 0xAA,	// MOV EAX, AAAAAAAA
		0x68, 0xBB, 0xBB, 0xBB, 0xBB,	// PUSH BBBBBBBB
		0xff, 0xd0,	// CALL EAX
		0x9D,	// POPFD
		0x61,	// POPAD
		0x68, 0xCC, 0xCC, 0xCC, 0xCC,	// PUSH CCCCCCCC
		0xc3	// RET
	};
	WOW64_CONTEXT ThreadContext;
#ifdef TARGET_WINXP
	// No Wow64GetThreadContext on Windows XP, so read it from TLS:
	// http://www.nynaeve.net/?p=191
	PWOW64_CONTEXT           RemoteCtx = NULL;
	CONTEXT                  Ctx64;
	SIZE_T                   Transferred = 0;

	TRACE("InjectDllHijackThreadX32(%08X, %08X, %S)...", hProc, hThread, DllName);
	Ctx64.ContextFlags = CONTEXT_CONTROL;
	GetThreadContext(hThread, &Ctx64);
	if (Ctx64.SegCs != WOW64_CS32)
	{
		THREAD_BASIC_INFORMATION ThreadInfo;

		if (!NT_SUCCESS(NtQueryInformationThread(hThread, ThreadBasicInformation, &ThreadInfo, sizeof(THREAD_BASIC_INFORMATION), 0)) ||
			!ReadProcessMemory(hProc, &((PTEB)ThreadInfo.TebBaseAddress)->TlsSlots[WOW64_TLS_CPURESERVED], &RemoteCtx, sizeof(RemoteCtx), &Transferred) ||
			Transferred != sizeof(RemoteCtx) ||
			!ReadProcessMemory(hProc, (PUCHAR)(RemoteCtx)+sizeof(ULONG), &ThreadContext, sizeof(ThreadContext), &Transferred) ||
			Transferred != sizeof(ThreadContext))
		{
			TRACE("Reading WOW64 thread context failed: %d\n", GetLastError());
			return FALSE;
		}
	}
	else
	{
		//ThreadContext.Eip = (ULONG)Ctx64.Rip;

		// When thread is in 64bit code/syscall, that may go wrong...
		TRACE("Reading WOW64 thread context failed: In Syscall\n");
		return FALSE;
	}
#else  // TARGET_WINXP
	ThreadContext.ContextFlags = CONTEXT_CONTROL;
	Wow64GetThreadContext(hThread, &ThreadContext);
#endif // TARGET_WINXP

	//allocate a remote buffer
	PBYTE InjData =
		(PBYTE)VirtualAllocEx(hProc, NULL, sizeof(code) + (wcslen(DllName) + 1)*sizeof(WCHAR),
			MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	DWORD dwLoadLibrary = GetLoadLibraryAddressX32(hProc);
	*(PDWORD)&code[3] = dwLoadLibrary;
	*(PDWORD)&code[8] = (DWORD)(InjData + sizeof(code));
	*(PDWORD)&code[17] = ThreadContext.Eip;

	//write the tmp buff + dll
	//Format: [RemoteFunction][DllName][null char]
	SIZE_T dwWritten;
	if (!WriteProcessMemory(hProc, InjData, code, sizeof(code), &dwWritten) ||
		!WriteProcessMemory(hProc, InjData + sizeof(code), DllName, (wcslen(DllName) + 1)*sizeof(WCHAR), &dwWritten))
	{
		TRACE("Writing memory failed: %d\n", GetLastError());
		return FALSE;
	}

	if (!isProcessInitialized(hProc))
	{
		/* process has not yet been initialized... As hijacking threads is a bit dangerous anyway,
		* we back out by queuing an APC instead. NtTestAlert() call in loader should call us just
		* in time in this case... */
		NTSTATUS Status;

		Status = RtlQueueApcWow64Thread(hThread, (PVOID)dwLoadLibrary, (ULONG_PTR)InjData + sizeof(code), NULL, NULL);
		if (NT_SUCCESS(Status))
		{
			TRACE("Queued loader injection via APC @%08X\n", dwLoadLibrary);
			return TRUE;
		}
		else
		{
			TRACE("QueueUserAPC failed: %d\n", Status);
		}
	}
	else
	{
		// FIXME: Too dangerous?
		return FALSE;
	}

	//set the EIP
	ThreadContext.Eip = (ULONG)InjData;
#ifdef TARGET_WINXP
	if (Ctx64.SegCs != WOW64_CS32 && RemoteCtx && Transferred == sizeof(ThreadContext))
	{
		if (WriteProcessMemory(hProc, (PUCHAR)(RemoteCtx)+sizeof(ULONG), &ThreadContext, sizeof(ThreadContext), &Transferred) && 
			Transferred == sizeof(ThreadContext))
		{
			TRACE("Success.\n");
			return TRUE;
		}
		TRACE("Writing memory failed: %d\n", GetLastError());
		return FALSE;
	}
	TRACE("Cannot write back x64 Context: SegCs=%08X, RemoteCtx=%08X, Transferred=%08X", Ctx64.SegCs, RemoteCtx, Transferred);
	return FALSE;
#else
	Wow64SetThreadContext(hThread, &ThreadContext);
#endif
	return TRUE;
}

HANDLE InjectLdntvdmWow64RemoteThread(HANDLE hProcess)
{
	LPTHREAD_START_ROUTINE pLoadLibraryW;

	TRACE("InjectLdntvdmWow64RemoteThread(%08X)...", hProcess);
	if (pLoadLibraryW = (LPTHREAD_START_ROUTINE)GetLoadLibraryAddressX32(hProcess))
	{
		PBYTE *pLibRemote;

		if (pLibRemote = VirtualAllocEx(hProcess, NULL, sizeof(LDNTVDM_NAME), MEM_COMMIT, PAGE_READWRITE))
		{
			HANDLE hThread;

			WriteProcessMemory(hProcess, pLibRemote, (void*)LDNTVDM_NAME, sizeof(LDNTVDM_NAME), NULL);
			hThread = CreateRemoteThread(hProcess, NULL, 0, pLoadLibraryW, pLibRemote, 0, NULL);
			if (hThread) {TRACE("hThread = %08X\n", hThread);}  else { TRACE("CreateRemoteThread failed with\n", GetLastError()); }
			return hThread;
		}
		else { TRACE("VirtualAlloc failed\n"); }
	}
	else { TRACE("GetLoadLibraryAddressX32 failed\n"); }
	return NULL;
}

DWORD WINAPI InjectLdntvdmWow64UsingRemoteThread(HANDLE hProcess)
{
	NTSTATUS Status;
	HANDLE hThread;

	if (NT_SUCCESS((Status = NtGetNextThread(hProcess, NULL, THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, 0, 0, &hThread))))
	{
		HANDLE hNewThread;

		SuspendThread(hThread);
		if (hNewThread = InjectLdntvdmWow64RemoteThread(hProcess))
		{
			int i;

			for (i = 0; i < 20; i++)
			{
				if (WaitForSingleObject(hNewThread, 50) == WAIT_OBJECT_0) break;
				InjectDllHijackThreadX32(hProcess, hThread, LDNTVDM_NAME);
				ResumeThread(hThread);
				break;
			}
			CloseHandle(hNewThread);
		}
		ResumeThread(hThread);
		CloseHandle(hThread);
		return TRUE;
	}
	else { TRACE("NtGetThread returned Status %08X\n", Status); }
	return FALSE;
}

DWORD WINAPI InjectLdntvdmWow64Thread(LPVOID lpPID)
{
	HANDLE hProcess;
	int i;
	HANDLE hThread;

	for (i = 0; i < 20; i++)
	{
		if (hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION, FALSE, (DWORD)lpPID))
		{
			hThread = InjectLdntvdmWow64RemoteThread(hProcess);
			CloseHandle(hProcess);
			if (hThread)
			{
				CloseHandle(hThread);
				break;
			}
		}
		else break;
		Sleep(250);
	}
	ExitThread(0);
}

#endif

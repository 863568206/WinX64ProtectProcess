#include "define.h"

NTSTATUS DriverEntry(PDRIVER_OBJECT pDriver, PUNICODE_STRING str) {
	pDriver->DriverUnload = DriverUnload;

	DbgPrint("G:DriverEntry\r\n");

	//pTable = (PServiceDescriptorTable)GetKeServiceDescriptorTable64();
	//DbgPrint("G:GetKeServiceDescriptorTable64: 0x%xd \r\n", GetKeServiceDescriptorTable64());
	//DbgPrint("G:GetFuncAddr: 0x%xd \r\n", GetFuncAddr(35));
	HookSSDT();
	return STATUS_SUCCESS;
}

void DriverUnload(PDRIVER_OBJECT pDriver) {
	//�������
	DbgPrint("DriverUnload...\n\r");
	UnhookSSDT();
}

//�����Լ���NtOpenProcess
NTSTATUS __stdcall Fake_NtOpenProcess(OUT PHANDLE  ProcessHandle,
	IN ACCESS_MASK  DesiredAccess,
	IN POBJECT_ATTRIBUTES  ObjectAttributes,
	IN OPTIONAL PCLIENT_ID  ClientId) {
	PEPROCESS process = NULL;
	NTSTATUS st = ObReferenceObjectByHandle(ClientId->UniqueProcess, 0, *PsProcessType, KernelMode, &process, NULL);
	//DbgPrint("����HOOK����.\r\nPsGetProcessImageFileName:");
	//DbgPrint((char*)PsGetProcessImageFileName(process));
	//DbgPrint("\r\n");
	if (NT_SUCCESS(st)) {
		if (!_stricmp((char*)PsGetProcessImageFileName(process), "calc.exe")) {
			DbgPrint("calc----calc\r\n");
			return STATUS_ACCESS_DENIED;
		} else {
			return OldOpenProcess(ProcessHandle, DesiredAccess, ObjectAttributes, ClientId);
		}
	} else {
		return STATUS_ACCESS_DENIED;
	}
	return OldOpenProcess(ProcessHandle, DesiredAccess, ObjectAttributes, ClientId);
}

//�ر�ҳ�汣��
KIRQL WPOFFx64() {
	KIRQL irql = KeRaiseIrqlToDpcLevel();
	UINT64 cr0 = __readcr0();
	cr0 &= 0xfffffffffffeffff;
	__writecr0(cr0);
	_disable();
	return irql;
}


//����ҳ�汣��
void WPONx64(KIRQL irql) {
	UINT64 cr0 = __readcr0();
	cr0 |= 0x10000;
	_enable();
	__writecr0(cr0);
	KeLowerIrql(irql);
}

ULONGLONG GetKeServiceDescriptorTable64() {
	PUCHAR StartSearchAddress = (PUCHAR)__readmsr(0xC0000082);
	PUCHAR EndSearchAddress = StartSearchAddress + 0x500;
	PUCHAR i = NULL;
	UCHAR byte1 = 0, byte2 = 0, byte3 = 0;
	ULONG temp = 0;
	ULONGLONG addr = 0;
	//��ʼ����
	for (i = StartSearchAddress; i < EndSearchAddress; i++) {
		if (MmIsAddressValid(i) && MmIsAddressValid(i + 1) && MmIsAddressValid(i + 2)) {
			byte1 = *i;
			byte2 = *(i + 1);
			byte3 = *(i + 2);
			if (byte1 == 0x4c && byte2 == 0x8d && byte3 == 0x15) //4c8d15
			{
				memcpy(&temp, i + 3, 4);
				addr = (ULONGLONG)temp + (ULONGLONG)i + 7;
				return addr;
			}
		}
	}
	return 0;
}


//����KeServiceDescriptorTable�ҵ�SSDT��ַ
PULONG GetSSDTBaseAddress() {
	PULONG addr = NULL;
	PSYSTEM_SERVICE_TABLE ssdt = (PSYSTEM_SERVICE_TABLE)GetKeServiceDescriptorTable64();
	DbgPrint("SSTD baseAddress: 0x%xd", ssdt);
	addr = (PULONG)(ssdt->ServiceTableBase);
	return addr;
}

//���ݱ���ҵ�SSDT���к����ĵ�ַ
ULONGLONG GetFuncAddr(ULONG id) {
	LONG dwtemp = 0;
	PULONG ServiceTableBase = NULL;
	ServiceTableBase = (PULONG)GetSSDTBaseAddress();
	dwtemp = ServiceTableBase[id];
	dwtemp = dwtemp >> 4;
	return (LONGLONG)dwtemp + (ULONGLONG)ServiceTableBase;
}

//���ú�����ƫ�Ƶ�ַ��ע�����в����Ĵ�������λ���˲���������4���������������С�ڵ���4��ʱ��Ϊ0
#define SETBIT(x,y) x|=(1<<y) //��X�ĵ�Yλ��1
#define CLRBIT(x,y) x&=~(1<<y) //��X�ĵ�Yλ��0
#define GETBIT(x,y) (x & (1 << y)) //ȡX�ĵ�Yλ������0���0
ULONG GetOffsetAddress(ULONGLONG FuncAddr, CHAR paramCount) {
	LONG dwtmp = 0, i;
	CHAR b = 0, bits[4] = { 0 };
	PULONG stb = NULL;
	stb = GetSSDTBaseAddress();
	dwtmp = (LONG)(FuncAddr - (ULONGLONG)stb);
	dwtmp = dwtmp << 4;
	if (paramCount > 4) {
		paramCount = paramCount - 4;
	} else {
		paramCount = 0;
	}
	memcpy(&b, &dwtmp, 1);
	for (i = 0; i < 4; i++) {
		bits[i] = GETBIT(paramCount, i);
		if (bits[i]) {
			SETBIT(b, i);
		} else {
			CLRBIT(b, i);
		}
	}
	memcpy(&dwtmp, &b, 1);
	return dwtmp;
}

//�ں����ò����ķ�����������ת��(�Լ���NtOpenProcess����KeBugCheckEx������Ȼ����KeBugCheckEx��������ҪHook��NtOpenProcess)
VOID FuckKeBugCheckEx() {
	KIRQL irql;
	ULONGLONG myfun;
	UCHAR jmp_code[] = "\xFF\x25\x00\x00\x00\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF";
	myfun = (ULONGLONG)Fake_NtOpenProcess;
	memcpy(jmp_code + 6, &myfun, 8);
	irql = WPOFFx64();
	memset(KeBugCheckEx, 0x90, 15);
	memcpy(KeBugCheckEx, jmp_code, 14);
	WPONx64(irql);
}

//Hook ssdt
VOID HookSSDT() {
	KIRQL irql;
	LONG dwtmp = 0;
	PULONG stb = NULL;
	//1.get old address
	OldOpenProcess = (NTOPENPROCESS)GetFuncAddr(35);
	DbgPrint("Old_NtOpenProcess:%llx", (ULONGLONG)OldOpenProcess);
	//2.show new address
	stb = GetSSDTBaseAddress();
	//3.get offset value
	dwtmp = GetOffsetAddress((ULONGLONG)KeBugCheckEx, 4);
	//set kebugcheckex
	FuckKeBugCheckEx();
	//4.record  old offset  value
	OldTpVal = stb[35];
	irql = WPOFFx64();
	stb[35] = GetOffsetAddress((ULONGLONG)KeBugCheckEx, 2);
	WPONx64(irql);
	DbgPrint("KeBugCheckEx:%llx", (ULONGLONG)KeBugCheckEx);
	DbgPrint("New_NtOpenProcess:%llx", GetFuncAddr(35));
}

//UN hook
VOID UnhookSSDT() {
	KIRQL irql;
	PULONG stb = NULL;
	stb = GetSSDTBaseAddress();
	//�Ϻ����ĵ�ַ���ƻ���
	irql = WPOFFx64();
	stb[35] = OldTpVal;
	WPONx64(irql);
}
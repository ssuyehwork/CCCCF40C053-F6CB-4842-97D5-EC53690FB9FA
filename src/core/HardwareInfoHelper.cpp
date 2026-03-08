#include "HardwareInfoHelper.h"
#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <vector>

#ifndef StorageDeviceProtocolSpecificProperty
#define StorageDeviceProtocolSpecificProperty 51
#endif
#ifndef ProtocolTypeNvme
#define ProtocolTypeNvme 17
#endif
#ifndef NvmeDataTypeIdentify
#define NvmeDataTypeIdentify 0
#endif

#pragma pack(push, 1)
typedef struct _ATA_IDENTIFY_DATA {
    unsigned short Reserved1[10];
    unsigned short SerialNumber[10];      // Word 10-19
    unsigned short Reserved2[235];
} ATA_IDENTIFY_DATA;
#pragma pack(pop)

static QString formatAtaString(const unsigned short* data, int wordLen) {
    QByteArray ba;
    for (int i = 0; i < wordLen; ++i) {
        ba.append((char)(data[i] >> 8));
        ba.append((char)(data[i] & 0xFF));
    }
    return QString::fromLatin1(ba).trimmed();
}

QString HardwareInfoHelper::getDiskPhysicalSerialNumber() {
    static QString cachedSerial;
    if (!cachedSerial.isEmpty()) return cachedSerial;

    QString appPath = QCoreApplication::applicationFilePath();
    QString drive = QFileInfo(appPath).absoluteDir().path().left(2);
    if (!drive.contains(":")) return "";

    QString volumePath = "\\\\.\\" + drive;
    HANDLE hVolume = CreateFileW((LPCWSTR)volumePath.utf16(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hVolume == INVALID_HANDLE_VALUE) return "";

    STORAGE_DEVICE_NUMBER sdn;
    DWORD br = 0;
    if (!DeviceIoControl(hVolume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &br, NULL)) {
        CloseHandle(hVolume);
        return "";
    }
    CloseHandle(hVolume);

    QString physicalPath = QString("\\\\.\\PhysicalDrive%1").arg(sdn.DeviceNumber);
    HANDLE hDisk = CreateFileW((LPCWSTR)physicalPath.utf16(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDisk == INVALID_HANDLE_VALUE) {
        hDisk = CreateFileW((LPCWSTR)physicalPath.utf16(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    }
    if (hDisk == INVALID_HANDLE_VALUE) return "";

    // 1. 尝试 NVMe
    struct NvmeQueryBuffer {
        STORAGE_PROPERTY_QUERY Query;
        STORAGE_PROTOCOL_SPECIFIC_DATA ProtocolData;
        BYTE IdentifyBuffer[4096];
    } nvmeReq = {};
    nvmeReq.Query.PropertyId = static_cast<STORAGE_PROPERTY_ID>(StorageDeviceProtocolSpecificProperty);
    nvmeReq.Query.QueryType = PropertyStandardQuery;
    nvmeReq.ProtocolData.ProtocolType = static_cast<STORAGE_PROTOCOL_TYPE>(ProtocolTypeNvme);
    nvmeReq.ProtocolData.DataType = NvmeDataTypeIdentify;
    nvmeReq.ProtocolData.ProtocolDataRequestValue = 1;
    nvmeReq.ProtocolData.ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    nvmeReq.ProtocolData.ProtocolDataLength = 4096;

    if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &nvmeReq, sizeof(nvmeReq), &nvmeReq, sizeof(nvmeReq), &br, NULL)) {
        cachedSerial = QString::fromLatin1((const char*)nvmeReq.IdentifyBuffer + 4, 20).trimmed();
    }

    // 2. 尝试 SATA/SMART
    if (cachedSerial.isEmpty()) {
        SENDCMDINPARAMS sci = {0};
        sci.irDriveRegs.bCommandReg = 0xEC; 
        struct {
            SENDCMDOUTPARAMS out;
            BYTE buffer[512];
        } smartOut = {0};
        if (DeviceIoControl(hDisk, SMART_RCV_DRIVE_DATA, &sci, sizeof(sci), &smartOut, sizeof(smartOut), &br, NULL)) {
            ATA_IDENTIFY_DATA* pData = (ATA_IDENTIFY_DATA*)smartOut.buffer;
            cachedSerial = formatAtaString(pData->SerialNumber, 10);
        }
    }

    // 3. 通用保底
    if (cachedSerial.isEmpty()) {
        STORAGE_PROPERTY_QUERY commonQuery = {StorageDeviceProperty, PropertyStandardQuery};
        STORAGE_DEVICE_DESCRIPTOR commonDesc = {0};
        if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &commonQuery, sizeof(commonQuery), &commonDesc, sizeof(commonDesc), &br, NULL)) {
            std::vector<char> buf(commonDesc.Size);
            if (DeviceIoControl(hDisk, IOCTL_STORAGE_QUERY_PROPERTY, &commonQuery, sizeof(commonQuery), buf.data(), (DWORD)buf.size(), &br, NULL)) {
                STORAGE_DEVICE_DESCRIPTOR* pf = (STORAGE_DEVICE_DESCRIPTOR*)buf.data();
                if (pf->SerialNumberOffset > 0) cachedSerial = QString::fromLatin1(buf.data() + pf->SerialNumberOffset).trimmed();
            }
        }
    }

    CloseHandle(hDisk);
    return cachedSerial;
}

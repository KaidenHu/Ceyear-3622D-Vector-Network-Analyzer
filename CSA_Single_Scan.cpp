#include <visa.h>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

#define TIMEOUT 50000  // 设置超时时间
#define BUFFER_SIZE 2048  // 响应缓冲区大小

using namespace std;

// 发送命令
ViStatus sendCommand(ViSession instr, const string& command) {
    ViStatus status;
    ViUInt32 retCnt;
    cout << "Sending command: " << command << endl;

    status = viWrite(instr, (ViBuf)command.c_str(), command.length(), &retCnt);
    if (status < VI_SUCCESS) {
        cerr << "Error writing command: " << command << endl;
        char statusMessage[256];
        viStatusDesc(instr, status, statusMessage);
        cerr << "Error description: " << statusMessage << endl;
    }
    return status;
}

// 读取响应
ViStatus readResponse(ViSession instr, char* response, size_t bufferSize) {
    ViUInt32 retCnt;
    ViStatus status;
    cout << "Reading response..." << endl;
    
    // 先清空缓冲区，确保不会影响后续读取
    viFlush(instr, VI_READ_BUF);

    status = viRead(instr, (ViBuf)response, 50, &retCnt);
    if (status < VI_SUCCESS) {
        cerr << "Error reading response." << endl;
        char statusMessage[256];
        viStatusDesc(instr, status, statusMessage);
        cerr << "Error description: " << statusMessage << endl;
    }
    response[retCnt] = '\0';  // 确保字符串结束
    cout << "Bytes Read: " << retCnt << endl;
    cout << "Response: " << response << endl;
    return status;
}

// 等待操作完成
ViStatus waitForOperationComplete(ViSession instr) {
    char response[BUFFER_SIZE];
    ViStatus status;
    
    while (true) {
        sendCommand(instr, "*OPC?");
        status = readResponse(instr, response, sizeof(response));
        
        if (status >= VI_SUCCESS && string(response).find("+1") != string::npos) {
            cout << "Operation complete confirmed." << endl;
            break;
        }
        
        cout << "Waiting for operation completion..." << endl;
        this_thread::sleep_for(chrono::seconds(1));  // 1秒后再次检查
    }

    return status;
}

int main() {
    ViSession rm = VI_NULL, instr = VI_NULL;
    ViStatus status;
    char response[BUFFER_SIZE];

    double FREQ_START = 0.5;  // 起始频率
    double FREQ_STOP = 3.0;   // 终止频率
    int SWIP_POINTS = 501;    // 扫描点数
    double IFBW = 300;        // 中频带宽
    double POWER = 0;         // 信号功率
    string fileName = "cal_1_4";  // 校准文件名，不加后缀

    // 打开 VISA 资源管理器
    status = viOpenDefaultRM(&rm);
    if (status < VI_SUCCESS) {
        cerr << "Failed to open VISA resource manager." << endl;
        return -1;
    }

    // 连接设备
    status = viOpen(rm, "TCPIP0::172.141.11.202::5025::SOCKET", VI_NULL, VI_NULL, &instr);
    if (status < VI_SUCCESS) {
        cerr << "Failed to open connection to the device." << endl;
        viClose(rm);
        return -1;
    }

    // 设置VISA选项
    viSetAttribute(instr, VI_ATTR_TMO_VALUE, TIMEOUT); // 设置超时时间
    viSetAttribute(instr, VI_ATTR_TERMCHAR_EN, VI_TRUE);  // 启用终止符
    viSetAttribute(instr, VI_ATTR_TERMCHAR, '\n');  // 设置终止符为换行符

    // 复位设备
    sendCommand(instr, "*RST");
    this_thread::sleep_for(chrono::milliseconds(500));  // 等待设备复位完成

    // 设置起始频率
    string freqStartCommand = "SENSe1:FREQuency:STARt " + to_string(FREQ_START) + "e+9\n";
    sendCommand(instr, freqStartCommand);
    this_thread::sleep_for(chrono::milliseconds(500));  // 确保命令有时间执行

    // 设置终止频率
    string freqStopCommand = "SENSe1:FREQuency:STOP " + to_string(FREQ_STOP) + "e+9\n";
    sendCommand(instr, freqStopCommand);
    this_thread::sleep_for(chrono::milliseconds(500));  // 确保命令有时间执行

    // 设置扫描点数
    string sweepPointsCommand = "SENSe1:SWEep:POINts " + to_string(SWIP_POINTS) + "\n";
    sendCommand(instr, sweepPointsCommand);
    this_thread::sleep_for(chrono::milliseconds(500));  // 确保命令有时间执行

    // 设置带宽
    string ifbwCommand = "SENSe1:BANDwidth:RESolution " + to_string(IFBW) + "\n";
    sendCommand(instr, ifbwCommand);
    this_thread::sleep_for(chrono::milliseconds(500));  // 确保命令有时间执行

    // 加载状态和校准数据文件 (.csa)
    string loadCSACommand = ":MMEMory:LOAD:CSARchive \"" + fileName + "\"";
    sendCommand(instr, loadCSACommand);
    this_thread::sleep_for(chrono::milliseconds(500));  // 等待设备执行

    // 设置扫描为单次扫描模式
    string scanModeCommand = ":INITiate:CONTinuous OFF\n";  // 设置为单次扫描模式
    sendCommand(instr, scanModeCommand);
    this_thread::sleep_for(chrono::milliseconds(500));  // 确保命令有时间执行

    // 触发单次扫描
    string triggerScanCommand = ":INITiate1:IMMediate\n";  // 触发单次扫描
    sendCommand(instr, triggerScanCommand);
    this_thread::sleep_for(chrono::milliseconds(500));  // 等待设备响应

    // 等待扫描完成
    waitForOperationComplete(instr);

    // 停止扫描 (防止连续扫描)
    sendCommand(instr, ":ABORt\n");  // 停止当前的扫描任务
    this_thread::sleep_for(chrono::milliseconds(500));  // 等待命令完成

    // 读取扫描数据
    sendCommand(instr, "CALCulate1:MEASure1:DATA? FDATA\n");
    this_thread::sleep_for(chrono::milliseconds(500));  // 等待设备响应
    readResponse(instr, response, sizeof(response));

    // 查询起始频率
    string queryFreqStartCommand = "SENSe1:FREQuency:STARt?\n";
    sendCommand(instr, queryFreqStartCommand);
    this_thread::sleep_for(chrono::milliseconds(500));  // 等待设备响应
    readResponse(instr, response, sizeof(response));

    // 查询终止频率
    string queryFreqStopCommand = "SENSe1:FREQuency:STOP?\n";
    sendCommand(instr, queryFreqStopCommand);
    this_thread::sleep_for(chrono::milliseconds(500));  // 等待设备响应
    readResponse(instr, response, sizeof(response));

    // 查询扫描点数
    sendCommand(instr, "SENSe1:SWEep:POINts?\n");
    this_thread::sleep_for(chrono::milliseconds(500));  
    readResponse(instr, response, sizeof(response));

    // 查询带宽
    sendCommand(instr, "SENSe1:BANDwidth:RESolution?\n");
    this_thread::sleep_for(chrono::milliseconds(500));  
    readResponse(instr, response, sizeof(response));

    // 关闭连接
    viClose(instr);
    viClose(rm);

    return 0;
}

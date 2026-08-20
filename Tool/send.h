#ifndef SEND_H
#define SEND_H

typedef struct {
    int id;
    int humidity;
    double temperature;
    int CO2;
    int light;
} SensorData;


struct msgbuf {
    long type;
    SensorData text;  // 改为单个变量，而不是数组
};// 结构体定义，包含消息类型和传感器数据

extern struct  msgbuf msg2;

int Send_Data_Quene();
#endif
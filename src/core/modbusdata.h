#ifndef MODBUSDATA_H
#define MODBUSDATA_H
#include <QString>
#include <QModbusDataUnit>

struct ModbusParameter
{
    quint16  address;              //寄存器地址
    QString  key;                  //Key值
    QString  name;                 //参数名称
    quint16  length;               //数据BIT位长度
    quint16  bitpos;               //BIT位偏移
    QString  access;               //读、写
    QModbusDataUnit::RegisterType  regType;         //寄存器类型
    quint64  value;               //参数数值
};

struct ModbusSturct
{
    quint16 address;               //寄存器地址
    int regCount;                  //寄存器个数
    bool isReadReg;                //读寄存器还是写寄存器
    QModbusDataUnit::RegisterType  regType;         //寄存器类型
    QList<ModbusParameter> spList;
};



/* 从寄存器值中获取指定位置、长度的值
     * regValue: 整个寄存器读取的值
     * valuePos: 获取值的起始位置
     * valueSize: 获取值的长度
     * paramValue: 输出参数值
    */
static bool getParamValue16(quint16 regValue, quint16 valuePos, quint16 valueSize, quint16 &paramValue)
{
    if((valuePos + valueSize) > 16)
    {
        return false;
    }
    paramValue = (quint16)((regValue >> valuePos) & (0xFFFF >> (16-valueSize)));
    return true;
}
static bool getParamValue32(quint32 regValue, quint16 valuePos, quint16 valueSize, quint32 &paramValue)
{
    if((valuePos + valueSize) > 32)
    {
        return false;
    }
    paramValue = (quint32)((regValue >> valuePos) & (0xFFFFFFFF >> (32-valueSize)));
    return true;
}
static bool getParamValue64(quint64 regValue, quint16 valuePos, quint16 valueSize, quint64 &paramValue)
{
    if((valuePos + valueSize) > 64)
    {
        return false;
    }
    paramValue = (quint64)((regValue >> valuePos) & (0xFFFFFFFFFFFFFFFF >> (64-valueSize)));
    return true;
}

/* 设置寄存器某位置的数值
     * oldRegValue: 寄存器读取的值
     * valuePos: 需求获取值的起始位置
     * valueSize: 获取值的长度
     * setValue: 设置的值
     * newRegValue: 输出新的寄存器值
    */
static bool setParamValue16(quint16 oldRegValue, quint16 valuePos, quint16 valueSize, quint16 setValue, quint16 &newRegValue)
{
    //判断设置的值是否大于最大值，也就是2的valueSize次方
    if(setValue >= pow(2, valueSize))
    {
        return false;
    }
    if((valuePos + valueSize) > 16)
    {
        return false;
    }

    newRegValue = (quint16)((setValue << valuePos) | ( (0xFFFF<<(valuePos+valueSize) | 0xFFFF>>(16-valuePos)) & oldRegValue));
    return true;
}
static bool setParamValue32(quint32 oldRegValue, quint16 valuePos, quint16 valueSize, quint32 setValue, quint32 &newRegValue)
{
    //判断设置的值是否大于最大值，也就是2的valueSize次方
    if(setValue >= pow(2, valueSize))
    {
        return false;
    }
    if((valuePos + valueSize) > 32)
    {
        return false;
    }

    newRegValue = (quint32)((setValue << valuePos) | ( (0xFFFFFFFF<<(valuePos+valueSize) | 0xFFFFFFFF>>(32-valuePos)) & oldRegValue));
    return true;
}
static bool setParamValue64(quint64 oldRegValue, quint16 valuePos, quint16 valueSize, quint64 setValue, quint64 &newRegValue)
{
    //判断设置的值是否大于最大值，也就是2的valueSize次方
    if(setValue >= pow(2, valueSize))
    {
        return false;
    }
    if((valuePos + valueSize) > 64)
    {
        return false;
    }

    newRegValue = (quint64)((setValue << valuePos) | ( (0xFFFFFFFFFFFFFFFF<<(valuePos+valueSize) | 0xFFFFFFFFFFFFFFFF>>(64-valuePos)) & oldRegValue));
    return true;
}


#endif // MODBUSDATA_H

// SerialGate.cpp
#include "SerialGate.h"
#include <QDebug>

namespace {
QString frameHex(const QByteArray& f)
{
    return QString::fromLatin1(f.toHex(' ').toUpper());
}
} // namespace

SerialGate::SerialGate(QObject* parent) : QObject(parent)
{
    _holdTimer.setSingleShot(true);
    connect(&_holdTimer, &QTimer::timeout, this, [this]() {
        if (_port.isOpen() && !sendFrame(_proto.cmdOff, _proto.channel))
            return;   // 关闸失败已发 errorText
        _gateOpen = false;
        emit gateClosed();
    });

    // 串口异常显式升级（4b）：驱动级错误（拔线/占用/校验错）不再静默，
    // 上报宿主置红提示重连
    connect(&_port, &QSerialPort::errorOccurred, this,
            [this](QSerialPort::SerialPortError err) {
                if (err == QSerialPort::NoError)
                    return;
                emit errorText(QStringLiteral("串口异常（%1）：%2，请检查接线后重连")
                                   .arg(int(err))
                                   .arg(_port.errorString()));
            });
}

bool SerialGate::sendFrame(unsigned char cmd, unsigned char data)
{
    QByteArray f;
    f.append(_proto.head);
    f.append(_proto.addr);
    f.append(cmd);
    f.append(data);
    unsigned char sum = 0;
    for (unsigned char c : f) sum += c;   // 校验和 = 前缀字节和的低 8 位
    f.append(sum);

    const qint64 n = _port.write(f);
    if (n != f.size() || !_port.flush()) {
        emit errorText(QStringLiteral("继电器指令写入失败: %1")
                           .arg(frameHex(f)));
        return false;
    }
    return true;
}

bool SerialGate::open(const QString& portName)
{
    close();
    _port.setPortName(portName);
    _port.setBaudRate(_proto.baud);
    _port.setDataBits(QSerialPort::Data8);
    _port.setParity(QSerialPort::NoParity);
    _port.setStopBits(QSerialPort::OneStop);
    if (!_port.open(QIODevice::ReadWrite)) {
        emit errorText(QStringLiteral("串口打开失败 %1: %2")
                           .arg(portName, _port.errorString()));
        return false;
    }
    return true;
}

void SerialGate::close()
{
    if (_holdTimer.isActive())
        _holdTimer.stop();
    if (_gateOpen) {           // 断开前尽量复位闸门
        if (isConnected())
            sendFrame(_proto.cmdOff, _proto.channel);
        _gateOpen = false;
        emit gateClosed();
    }
    if (_port.isOpen())
        _port.close();         // 之后 isSimulated() == true，回到模拟模式
}

void SerialGate::pulseOpen(int holdMs)
{
    if (_gateOpen)
        return;                // 已在开闸周期内，忽略重复触发
    _gateOpen = true;
    emit gateOpened();

    if (isConnected() && !sendFrame(_proto.cmdOn, _proto.channel))
        return;                // 开闸失败：状态保持 open 由 holdTimer 关闭
    _holdTimer.start(holdMs);
}

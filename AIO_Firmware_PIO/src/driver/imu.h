#ifndef IMU_H
#define IMU_H

#include <I2Cdev.h>
#include <MPU6050.h>
#include "lv_port_indev.h"
#include <list>
#define ACTION_HISTORY_BUF_LEN 5

// ---- 动作识别阈值（加速度原始值，±2g 量程下 16384 = 1g）----
// 原值: TURN 4000(~14°), UP/DOWN 5000(~18°), SHAKE 1000(~3.5°)
#define IMU_TURN_TH 5000    // 左右倾 ~18°
#define IMU_UPDOWN_TH 5500  // 前倾/后仰 ~20°
#define IMU_SHAKE_TH 2500   // 震动 ~9°（原 1000 太敏感，静止时也会触发）
#define IMU_TURN_CONFIRM 2  // 左右倾需连续 2 次采样(约 400ms)一致；设 1 恢复原行为
#define IMU_REST_MAX 3000   // 静止零点补偿的最大幅度
#define IMU_DEBUG_RAW 0     // 1: 每秒打印一次去零点后的 ax/ay/az，便于调阈值

extern int32_t encoder_diff;
extern lv_indev_state_t encoder_state;

extern const char *active_type_info[];

enum ACTIVE_TYPE
{
    TURN_RIGHT = 0,
    RETURN,
    TURN_LEFT,
    UP,
    DOWN,
    GO_FORWORD,
    SHAKE,
    UNKNOWN
};

// 方向类型
enum MPU_DIR_TYPE
{
    NORMAL_DIR_TYPE = 0,
    X_DIR_TYPE = 0x01,
    Y_DIR_TYPE = 0x02,
    Z_DIR_TYPE = 0x04,
    XY_DIR_TYPE = 0x08
};

struct SysMpuConfig
{
    int16_t x_gyro_offset;
    int16_t y_gyro_offset;
    int16_t z_gyro_offset;

    int16_t x_accel_offset;
    int16_t y_accel_offset;
    int16_t z_accel_offset;
};

struct ImuAction
{
    volatile ACTIVE_TYPE active;
    boolean isValid;
    boolean long_time;
    int16_t v_ax; // v表示虚拟参数（用于调整6050的初始方位）
    int16_t v_ay;
    int16_t v_az;
    int16_t v_gx;
    int16_t v_gy;
    int16_t v_gz;
};

class IMU
{
private:
    MPU6050 mpu;
    int flag;
    long last_update_time;
    uint8_t order; // 表示方位，x与y是否对换
    int16_t rest_ax; // 静止时的残余加速度（校准后采样得到），用于抵消零偏/桌面倾斜
    int16_t rest_ay;

public:
    ImuAction action_info;
    // 用来储存历史动作
    // std::list<ACTIVE_TYPE> act_info_history;
    ACTIVE_TYPE act_info_history[ACTION_HISTORY_BUF_LEN];
    int act_info_history_ind; // 标志储存的位置

public:
    IMU();
    void init(uint8_t order, uint8_t auto_calibration,
              SysMpuConfig *mpu_cfg);
    void setOrder(uint8_t order); // 设置方向
    bool Encoder_GetIsPush(void); // 适配Peak的编码器中键 开关机使用
    ImuAction *getAction(void); // 获取动作
    void getVirtureMotion6(ImuAction *action_info);
};

#endif

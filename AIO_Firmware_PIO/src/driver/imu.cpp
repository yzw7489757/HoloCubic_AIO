#include "imu.h"
#include "common.h"

const char *active_type_info[] = {"TURN_RIGHT", "RETURN",
                                  "TURN_LEFT", "UP",
                                  "DOWN", "GO_FORWORD",
                                  "SHAKE", "UNKNOWN"};

IMU::IMU()
{
    action_info.isValid = false;
    action_info.active = ACTIVE_TYPE::UNKNOWN;
    action_info.long_time = true;
    // 初始化数据
    for (int pos = 0; pos < ACTION_HISTORY_BUF_LEN; ++pos)
    {
        // act_info_history.push_back(UNKNOWN);
        act_info_history[pos] = UNKNOWN;
    }
    act_info_history_ind = ACTION_HISTORY_BUF_LEN - 1;
    this->order = 0; // 表示方位
    rest_ax = 0;
    rest_ay = 0;
}

void IMU::init(uint8_t order, uint8_t auto_calibration,
               SysMpuConfig *mpu_cfg)
{
    this->setOrder(order); // 设置方向
    Wire.begin(IMU_I2C_SDA, IMU_I2C_SCL);
    Wire.setClock(400000);
    unsigned long timeout = 5000;
    unsigned long preMillis = GET_SYS_MILLIS();
    // mpu = MPU6050(0x68, &Wire);
    mpu = MPU6050(0x68);
    while (!mpu.testConnection() && !doDelayMillisTime(timeout, &preMillis, false))
        ;

    if (!mpu.testConnection())
    {
        Serial.print(F("Unable to connect to MPU6050.\n"));
        return;
    }

    Serial.print(F("Initialization MPU6050 now, Please don't move.\n"));
    mpu.initialize();

    if (auto_calibration == 0)
    {
        // supply your own gyro offsets here, scaled for min sensitivity
        mpu.setXGyroOffset(mpu_cfg->x_gyro_offset);
        mpu.setYGyroOffset(mpu_cfg->y_gyro_offset);
        mpu.setZGyroOffset(mpu_cfg->z_gyro_offset);
        mpu.setXAccelOffset(mpu_cfg->x_accel_offset);
        mpu.setYAccelOffset(mpu_cfg->y_accel_offset);
        mpu.setZAccelOffset(mpu_cfg->z_accel_offset); // 1688 factory default for my test chip
    }
    else
    {
        // 启动自动校准
        // 7次循环自动校正
        mpu.CalibrateAccel(7);
        mpu.CalibrateGyro(7);
        mpu.PrintActiveOffsets();

        mpu_cfg->x_gyro_offset = mpu.getXGyroOffset();
        mpu_cfg->y_gyro_offset = mpu.getYGyroOffset();
        mpu_cfg->z_gyro_offset = mpu.getZGyroOffset();
        mpu_cfg->x_accel_offset = mpu.getXAccelOffset();
        mpu_cfg->y_accel_offset = mpu.getYAccelOffset();
        mpu_cfg->z_accel_offset = mpu.getZAccelOffset();
    }

    // 采样静止零点：校准不完美或桌面略倾斜时，ax/ay 静止值可能超过 SHAKE 阈值，导致菜单自己乱切。
    // 只在残余量不大时(|x|<IMU_REST_MAX)才记录，避免开机时手拿着倾斜导致零点错误。
    {
        long sx = 0, sy = 0;
        const int n = 20;
        for (int i = 0; i < n; i++)
        {
            ImuAction t;
            getVirtureMotion6(&t);
            sx += t.v_ax;
            sy += t.v_ay;
            delay(5);
        }
        sx /= n;
        sy /= n;
        rest_ax = (abs(sx) < IMU_REST_MAX) ? sx : 0;
        rest_ay = (abs(sy) < IMU_REST_MAX) ? sy : 0;
        Serial.printf("[IMU] rest ax=%ld ay=%ld -> bias ax=%d ay=%d\n", sx, sy, rest_ax, rest_ay);
    }

    Serial.print(F("Initialization MPU6050 success.\n"));
}

void IMU::setOrder(uint8_t order) // 设置方向
{
    this->order = order; // 表示方位
}

bool IMU::Encoder_GetIsPush(void)
{
#ifdef PEAK
    return (digitalRead(CONFIG_ENCODER_PUSH_PIN) == LOW);
#else
    return false;
#endif
}

ImuAction *IMU::getAction(void)
{
    // 基本方法: 通过对近来的动作数据简单的分析，确定出动作的类型
    ImuAction tmp_info;
    getVirtureMotion6(&tmp_info);

    // Serial.printf("gx = %d\tgy = %d\tgz = %d", tmp_info.v_gx, tmp_info.v_gy, tmp_info.v_gz);
    // Serial.printf("\tax = %d\tay = %d\taz = %d\n", tmp_info.v_ax, tmp_info.v_ay, tmp_info.v_az);

    tmp_info.active = ACTIVE_TYPE::UNKNOWN;
    // 去掉静止零点
    tmp_info.v_ax -= rest_ax;
    tmp_info.v_ay -= rest_ay;
#if IMU_DEBUG_RAW
    {
        static unsigned long last = 0;
        if (millis() - last > 1000)
        {
            last = millis();
            Serial.printf("[IMU] ax=%d ay=%d az=%d\n", tmp_info.v_ax, tmp_info.v_ay, tmp_info.v_az);
        }
    }
#endif

    // 原先判断的只是加速度，现在要加上陀螺仪
    if (ACTIVE_TYPE::UNKNOWN == tmp_info.active)
    {
        if (tmp_info.v_ay > IMU_TURN_TH)
        {
            tmp_info.active = ACTIVE_TYPE::TURN_LEFT;
        }
        else if (tmp_info.v_ay < -IMU_TURN_TH)
        {
            tmp_info.active = ACTIVE_TYPE::TURN_RIGHT;
        }
        else if (tmp_info.v_ay > IMU_SHAKE_TH || tmp_info.v_ay < -IMU_SHAKE_TH)
        {
            // 震动检测
            tmp_info.active = ACTIVE_TYPE::SHAKE;
        }
    }

    if (ACTIVE_TYPE::UNKNOWN == tmp_info.active)
    {
        if (tmp_info.v_ax > IMU_UPDOWN_TH)
        {
            tmp_info.active = ACTIVE_TYPE::UP;
        }
        else if (tmp_info.v_ax < -IMU_UPDOWN_TH)
        {
            tmp_info.active = ACTIVE_TYPE::DOWN;
        }
        // 原代码这里误用了 action_info.v_ax（上一次的结构体，未更新），改为本次采样
        else if (tmp_info.v_ax > IMU_SHAKE_TH || tmp_info.v_ax < -IMU_SHAKE_TH)
        {
            // 震动检测
            tmp_info.active = ACTIVE_TYPE::SHAKE;
        }
    }

    // 储存当前检测的动作数据到动作缓冲区中
    act_info_history_ind = (act_info_history_ind + 1) % ACTION_HISTORY_BUF_LEN;
    int index = act_info_history_ind;
    act_info_history[index] = tmp_info.active;

    // 本次流程的动作识别
    if (!action_info.isValid)
    {
        bool isHoldDown = false; // 长按的标志位
        // 本次流程的动作识别
        int second = (index + ACTION_HISTORY_BUF_LEN - 1) % ACTION_HISTORY_BUF_LEN;
        int third = (index + ACTION_HISTORY_BUF_LEN - 2) % ACTION_HISTORY_BUF_LEN;
        // 先识别"短按" （注：不要写成else if）
        // 左右倾需要连续 IMU_TURN_CONFIRM 次采样(每次200ms)一致才算，过滤桌面震动/噪声
        bool confirmed = true;
        if (IMU_TURN_CONFIRM >= 2 && (ACTIVE_TYPE::TURN_LEFT == tmp_info.active ||
                                      ACTIVE_TYPE::TURN_RIGHT == tmp_info.active))
        {
            confirmed = (act_info_history[second] == tmp_info.active);
        }
        if (ACTIVE_TYPE::UNKNOWN != tmp_info.active && confirmed)
        {
            action_info.isValid = 1;
            action_info.active = tmp_info.active;
        }
        // 识别"长按","长按"相对"短按"高级（所以键值升级放在短按之后）
        if (act_info_history[index] == act_info_history[second] && act_info_history[second] == act_info_history[third])
        {
            // 目前只识别前后的长按
            if (ACTIVE_TYPE::UP == tmp_info.active)
            {
                isHoldDown = true;
                action_info.isValid = 1;
                action_info.active = ACTIVE_TYPE::GO_FORWORD;
            }
            else if (ACTIVE_TYPE::DOWN == tmp_info.active)
            {
                isHoldDown = true;
                action_info.isValid = 1;
                action_info.active = ACTIVE_TYPE::RETURN;
            }
            // 如需左右的长按可在此处添加"else if"的逻辑

            if (isHoldDown)
            {
                // 本次识别为长按，则手动清除识别过的历史数据 避免对下次动作识别的影响
                act_info_history[second] = ACTIVE_TYPE::UNKNOWN;
                act_info_history[third] = ACTIVE_TYPE::UNKNOWN;
            }
        }
    }

    return &action_info;
}

void IMU::getVirtureMotion6(ImuAction *action_info)
{
    mpu.getMotion6(&(action_info->v_ax), &(action_info->v_ay),
                   &(action_info->v_az), &(action_info->v_gx),
                   &(action_info->v_gy), &(action_info->v_gz));

    if (order & X_DIR_TYPE)
    {
        action_info->v_ax = -action_info->v_ax;
        action_info->v_gx = -action_info->v_gx;
    }

    if (order & Y_DIR_TYPE)
    {
        action_info->v_ay = -action_info->v_ay;
        action_info->v_gy = -action_info->v_gy;
    }

    if (order & Z_DIR_TYPE)
    {
        action_info->v_az = -action_info->v_az;
        action_info->v_gz = -action_info->v_gz;
    }

    if (order & XY_DIR_TYPE)
    {
        int16_t swap_tmp;
        swap_tmp = action_info->v_ax;
        action_info->v_ax = action_info->v_ay;
        action_info->v_ay = swap_tmp;
        swap_tmp = action_info->v_gx;
        action_info->v_gx = action_info->v_gy;
        action_info->v_gy = swap_tmp;
    }
}
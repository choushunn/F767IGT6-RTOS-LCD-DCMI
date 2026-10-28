#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

/* 注册指针型输入设备（本阶段 read 回调固定返回 RELEASED，触摸驱动后续接入） */
void lv_port_indev_init(void);

#endif /* LV_PORT_INDEV_H */

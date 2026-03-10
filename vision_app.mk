################################################################################
# vision_app - 视觉识别应用
################################################################################

VISION_APP_SITE = $(TOPDIR)/smart_software/apps/vision_app
VISION_APP_SITE_METHOD = local

define VISION_APP_INSTALL_TARGET_CMDS
    # 将 Python 代码安装到板子的 /opt/vision_app/ 目录
    mkdir -p $(TARGET_DIR)/opt/vision_app
    cp -r $(@D)/src/* $(TARGET_DIR)/opt/vision_app/
    
    # 安装启动脚本
    install -m 755 $(@D)/scripts/start_app.sh $(TARGET_DIR)/usr/bin/
endef

$(eval $(generic-package))
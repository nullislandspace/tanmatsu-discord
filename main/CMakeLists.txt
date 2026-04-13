idf_build_get_property(idf_target IDF_TARGET)
if("${idf_target}" STREQUAL "esp32p4")
list(APPEND extra_requires esp-hosted-tanmatsu)
endif()


idf_component_register(
	SRCS
		"main.c"
		"config.c"
		"theme.c"
		"icons.c"
		"intfs.c"
		"msgstore.c"
		"backfill.c"
		"file_browser.c"
		"preview.c"
		"sdcard.c"
		"fbdraw.c"
		"discord_task.c"
		"ui_core.c"
		"attach_upload.c"
	PRIV_REQUIRES
		esp_lcd
		fatfs
		sdmmc
		esp_driver_sdmmc
		wear_levelling
		esp_partition
		esp_driver_jpeg
		vfs
		nvs_flash
		bt
		badge-bsp
		tanmatsu-wifi
		json
		esp_http_client
		esp-discord
		pax-codecs
		${extra_requires}
	INCLUDE_DIRS
		"."
)

idf_build_set_property(COMPILE_OPTIONS "-Wno-error=unused-variable" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=unused-const-variable" APPEND)

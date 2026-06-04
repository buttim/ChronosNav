#include <MD_KeySwitch.h>
#include <ChronosESP32.h>
#include <TFT_eSPI.h>
#include <Ticker.h>
#include <rom/gpio.h>
#include "NotoSansMonoSCB20.h"
#include "NotoSansBold36.h"

const int HEIGHT = 135, WIDTH = 240,
          LEDC_CHAN_A = 0, LEDC_CHAN_B = 1,
          PWM_RES = 8, DUTY_ON = 128, DUTY_OFF = 0,
          srcW = 48, srcH = 48, destW = 96, destH = 96;

const gpio_num_t TFT_BL_PIN = GPIO_NUM_4,
                 BAT_ADC_PIN = GPIO_NUM_34,
                 BUTTON_TOP_PIN = GPIO_NUM_35, BUTTON_BTM_PIN = GPIO_NUM_0,
                 BUZZER_PIN_A = GPIO_NUM_12, BUZZER_PIN_B = GPIO_NUM_13;

ChronosESP32 watch("Chronos Nav", CS_135x240_114_RTF);
MD_KeySwitch buttonTop(BUTTON_TOP_PIN, LOW), buttonBtm(BUTTON_BTM_PIN, LOW);
TFT_eSPI tft = TFT_eSPI(HEIGHT, WIDTH);
TFT_eSprite img = TFT_eSprite(&tft);
Ticker tickBuzzOff;
String title, eta, duration, distance, directions("---"), notificationIconType, notificationTitle("NO MESSAGES"), notificationMessage;
bool showETA = false, connected = false, navigation = false, lostConnection = false, find = false, notification = false;
int battery = 0;

const char *notifTable[] = {
	"TIME",
	"RTW",
	"HR24",
	"LANG",
	"RST",
	"CLR",
	"HOURLY",
	"FIND",
	"USER",
	"ALARM",
	"FONT",
	"SED",
	"SLEEP",
	"QUIET",
	"WATER",
	"WEATHER",
	"CAMERA",
	"PBAT",
	"APP",
	"QR",
	"NAV_DATA",
	"NAV_ICON",
	"CONTACT",
	"SYNCED"
};

void drawBattery(int percent) {
	if (percent > 100) percent = 100;
	if (percent < 0) percent = 0;

	const int batW = 35, batH = 18, nipW = 3,
	          canvasW = batW + nipW, canvasH = batH + 4,
	          xStart = 6, yStart = tft.height() - canvasH - 4;

	uint16_t barColor = TFT_GREEN;
	if (percent <= 20) barColor = TFT_RED;
	else if (percent <= 50) barColor = TFT_YELLOW;

	TFT_eSprite img = TFT_eSprite(&tft);
	if (img.createSprite(canvasW, canvasH, 8) == nullptr) return;

	img.fillSprite(TFT_NAVY);
	img.drawRect(0, 2, batW, batH, TFT_WHITE);
	img.fillRect(batW, 2 + (batH / 4), nipW, batH / 2, TFT_WHITE);

	const int maxInnerW = batW - 4;
	const int filledW = (maxInnerW * percent) / 100;
	if (filledW > 0) img.fillRect(2, 4, filledW, batH - 4, barColor);

	img.pushSprite(xStart, yStart);
	img.deleteSprite();
}

void redraw() {
	tft.fillScreen(TFT_NAVY);
	if (connected) {
		if (navigation) {
			img.pushSprite(0, 0);  //??
			tft.drawRightString(title, tft.width() - 2, 5, 0);
			tft.drawRightString(distance, tft.width() - 2, 60, 0);
			tft.fillRect(50, 100, tft.width(), tft.height(), TFT_NAVY);
			if (showETA) {
				tft.unloadFont();
				tft.drawRightString(eta, tft.width() - 2, 105, 4);
				tft.loadFont(NotoSansBold36);
			} else
				tft.drawRightString(duration, tft.width() - 2, 100, 0);
		} else
			tft.drawCentreString("Connected", tft.width() / 2, 20, 0);
	} else {
		tft.drawString("CHRONOS", 20, 5);
		tft.drawString("NAV", 20, 40);
	}
	drawBattery(battery);
}

void initBuzzer() {
	ledcAttach(BUZZER_PIN_A, 2000, PWM_RES);
	ledcAttach(BUZZER_PIN_B, 2000, PWM_RES);

	// CRITICAL: Invert the peripheral output on Channel B
	// This creates the 180-degree phase shift needed for differential drive
	ledcOutputInvert(BUZZER_PIN_B, true);
}

void playTone(uint32_t frequency, uint32_t duration_ms) {
	ledcChangeFrequency(BUZZER_PIN_A, frequency, PWM_RES);
	ledcChangeFrequency(BUZZER_PIN_B, frequency, PWM_RES);

	// Start the differential push-pull wave by turning on the duty cycles
	ledcWrite(BUZZER_PIN_A, DUTY_ON);
	ledcWrite(BUZZER_PIN_B, DUTY_ON);
	digitalWrite(TFT_BL_PIN, LOW);

	tickBuzzOff.once_ms(duration_ms, []() {
		digitalWrite(TFT_BL_PIN, HIGH);
		ledcWrite(BUZZER_PIN_A, DUTY_OFF);
		ledcWrite(BUZZER_PIN_B, DUTY_OFF);
	});
}

void showNotificationPopup(const String &iconType, const String &title, const String &message) {
	const int screenW = tft.width(), screenH = tft.height();
	const int padding = 8, iconDim = 24, iconX = padding, iconY = padding + 5;

	tft.fillScreen(TFT_BLACK);

	int color = TFT_WHITE;
	if (iconType.equalsIgnoreCase("whatsapp")) color = TFT_GREEN;
	else if (iconType.equalsIgnoreCase("telegram")) color = TFT_CYAN;

	tft.fillCircle(iconX + iconDim / 2, iconY + iconDim / 2, iconDim / 2, color);

	tft.loadFont(NotoSansMonoSCB20);
	tft.setTextColor(TFT_WHITE, TFT_BLACK);
	tft.setTextDatum(TL_DATUM);

	tft.drawString(title, iconX + iconDim + 10, iconY + 2);
	tft.drawFastHLine(padding, iconY + iconDim + 10, screenW - (padding * 2), TFT_DARKGREY);
	tft.drawString(message, padding, iconY + iconDim + 20);
	tft.unloadFont();
	tft.loadFont(NotoSansBold36);
	tft.setTextColor(TFT_YELLOW, TFT_NAVY, true);
}

void drawNavIcon(int xStart, int yStart, uint8_t *bitmapData, uint16_t fgColor, uint16_t bgColor) {
	if (bitmapData == nullptr) return;

	img.fillSprite(bgColor);

	int bytesPerRow = 6;

	for (int row = 0; row < srcH; row++)
		for (int col = 0; col < srcW; col++) {

			// Calculate layout coordinates within native 6-byte stride rows
			int byteIndex = (row * bytesPerRow) + (col / 8);
			int bitIndex = col % 8;

			// Unpack bits sequentially (MSB-first)
			bool isForeground = (bitmapData[byteIndex] & (0x80 >> bitIndex)) != 0;

			if (isForeground) {
				int targetX = col * 2;
				int targetY = row * 2;

				img.fillRect(targetX, targetY, 2, 2, fgColor);
			}
		}

	img.pushSprite(xStart, yStart);
}

float getBatteryVoltage() {
	uint32_t rawSum = 0;
	for (int i = 0; i < 20; i++) {
		rawSum += analogRead(BAT_ADC_PIN);
		delay(2);
	}
	float rawAverage = rawSum / 20.0;

	// Convert raw reading to Volts and adjust for the 2x hardware divider factor
	// 3.3V / 4095.0 * 2 = 0.001611. We use ~0.00175 to calibrate for ESP32 ADC non-linearity.
	float voltage = rawAverage * 0.00175;
	return voltage;
}

int getBatteryPercentage(float voltage) {
	int percentage = (int)((voltage - 3.3) / (4.2 - 3.3) * 100);

	if (percentage > 100) percentage = 100;
	if (percentage < 0) percentage = 0;

	return percentage;
}

void connectionCallback(bool state) {
	tft.fillRect(0, 0, tft.width(), tft.height(), TFT_NAVY);
	connected = state;
	redraw();
	if (!connected)
		lostConnection = true;
}

void notificationCallback(Notification n) {
	Serial.printf("Notification: \"%s\",app: \"%s\", icon: %d\n",
	              n.time.c_str(), n.app.c_str(), n.icon);
	Serial.printf("%s/%s\n", n.title.c_str(), n.message.c_str());
	notificationIconType = n.app;
	notificationTitle = n.title;
	notificationMessage = n.message;
	notification = true;
}

void configCallback(Config config, uint32_t a, uint32_t b) {
	switch (config) {
		case CF_NAV_DATA:
			//~ Serial.print("Navigation state: ");
			//~ Serial.println(a ? "Active" : "Inactive");
			navigation = a;
			if (navigation) {
				Navigation nav = watch.getNavigation();

				title = nav.title;
				distance = nav.distance;
				duration = nav.duration;
				eta = nav.eta;
				directions = nav.directions;
				//~ Serial.printf("directions: %s ETA: %s duration: %s\n", nav.directions.c_str(), nav.eta.c_str(), nav.duration.c_str());
				//~ Serial.printf("distance: %s title: %s speed: %s\n", nav.distance.c_str(), nav.title.c_str(), nav.speed.c_str());
			}
			redraw();
			break;
		case CF_NAV_ICON:
			//~ Serial.print("Navigation Icon data, position: ");
			//~ Serial.println(a);
			if (a == 2) {
				Navigation nav = watch.getNavigation();
				drawNavIcon(0, 0, (uint8_t *)nav.icon, TFT_GREEN, TFT_BLACK);
				playTone(1000, 100);
			}
			break;
		case CF_FIND:
			find = true;
			break;
		default:
			Serial.printf("configCallback: notifica %s a=%d b=%d\n", notifTable[config], a, b);
			break;
	}
}

void setup() {
	Serial.begin(115200);

	tft.init();
	tft.setRotation(1);
	tft.loadFont(NotoSansBold36);
	tft.setTextColor(TFT_YELLOW, TFT_NAVY, true);
	img.createSprite(destW, destH, 8);
	redraw();

	watch.setConnectionCallback(connectionCallback);
	watch.setNotificationCallback(notificationCallback);
	watch.setConfigurationCallback(configCallback);
	watch.begin();

	buttonTop.begin();
	buttonTop.enableRepeat(false);
	buttonTop.enableLongPress(true);
	buttonBtm.begin();
	buttonBtm.enableRepeat(false);
	buttonBtm.setLongPressTime(1500);
	buttonBtm.enableLongPress(true);

	initBuzzer();
	playTone(1000, 200);
}

void loop() {
	static uint64_t tLastBattRead = 0;

	if (tLastBattRead == 0 || millis() - tLastBattRead > 2000) {
		tLastBattRead = millis();
		tft.setCursor(0, tft.height() - 16);
		float v = getBatteryVoltage();
		battery = getBatteryPercentage(v);
		drawBattery(battery);
		watch.setBattery(battery);
	}

	watch.loop();

	if (notification) {
		notification = false;
		showNotificationPopup(notificationIconType, notificationTitle, notificationMessage);
		playTone(1000, 100);
		delay(3000);
		redraw();
	}

	if (find) {
		find = false;
		for (int i = 0; i < 3; i++) {
			playTone(587, 250);  // Re (D5)
			delay(300);
			playTone(659, 250);  // Mi (E5)
			delay(300);
			playTone(523, 250);  // Do (C5)
			delay(300);
			playTone(262, 250);  // Do Low (C4)
			delay(300);
			playTone(392, 800);  // Sol (G4) - Long sustained ending
			delay(1500);
		}
	}

	if (lostConnection) {
		lostConnection = false;
		bool stop = false;
		for (int i = 0; i < 300 && !stop; i++) {
			playTone(1000, 50);
			uint64_t t0 = millis();
			while (millis() - t0 < 100)
				if (buttonTop.read() == MD_KeySwitch::KS_PRESS || buttonBtm.read() == MD_KeySwitch::KS_PRESS) {
					Serial.println("STOP");
					stop = true;
					break;
				}
		}
	}

	switch (buttonTop.read()) {
		case MD_KeySwitch::KS_PRESS:
			showNotificationPopup(notificationIconType, notificationTitle, notificationMessage);
			delay(3000);
			redraw();
			break;
		case MD_KeySwitch::KS_LONGPRESS:
			showNotificationPopup("Navigation", "Navigation", directions);
			delay(3000);
			redraw();
			break;
	}

	switch (buttonBtm.read()) {
		case MD_KeySwitch::KS_PRESS:
			showETA = !showETA;
			redraw();
			break;
		case MD_KeySwitch::KS_LONGPRESS:
			esp_sleep_enable_ext0_wakeup(BUTTON_BTM_PIN, 0);
			tft.writecommand(0x10);  //OFF
			pinMode(TFT_BL_PIN, OUTPUT);
			digitalWrite(TFT_BL_PIN, LOW);
			digitalWrite(BUZZER_PIN_A, LOW);
			digitalWrite(BUZZER_PIN_B, LOW);
			while (digitalRead(BUTTON_BTM_PIN) == LOW)
				;
			delay(100);
			esp_deep_sleep_start();
			break;
	}
}

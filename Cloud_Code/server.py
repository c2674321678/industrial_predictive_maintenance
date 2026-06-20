from flask import Flask, request, jsonify
from sklearn.ensemble import IsolationForest
import numpy as np
import datetime
import json
import threading
import paho.mqtt.client as mqtt
from collections import deque
import time

app = Flask(__name__)

# =========================
# 配置区
# =========================

# 阈值
VIBRATION_WARN_THRESHOLD = 0.15   # 0.15g
VIBRATION_CRITICAL_THRESHOLD = 0.30  # 0.30g
TEMP_WARN_THRESHOLD = 33
TEMP_CRITICAL_THRESHOLD = 35


# 百度云 MQTT 配置
MQTT_BROKER = "ahslpcl.iot.gz.baidubce.com"
MQTT_PORT = 1883
MQTT_CLIENT_ID = "Server"
MQTT_USERNAME = "thingidp@ahslpcl|Server|0|MD5"
MQTT_PASSWORD = "4cfee8af3ba4b6fa6f5c7985fc08dcad"
MQTT_TOPIC_SUB = "$iot/Server/msg"
MQTT_TOPIC_PUB = "$iot/Server/events"

# 历史窗口
history = deque(maxlen=20)

mqtt_client = None

# =========================
# 训练隔离森林模型（基于你的实际正常数据）
# =========================

# 从你的日志中提取的正常数据点
# 更新后的正常数据点（物理值 g）
normal_samples = [
    # [振动幅值(g), 温度(°C), |acc_z|(g)]
    [0.00, 29.3, 0.00],
    [0.00, 29.3, 0.00],
    [0.00, 29.4, 0.00],
    [0.01, 29.6, 0.01],
    [0.00, 30.3, 0.00],
    [0.01, 30.5, 0.01],
    [0.00, 30.6, 0.00],
    [0.01, 30.8, 0.01],
    [0.00, 31.2, 0.00],
    [0.01, 31.3, 0.01],
    [0.00, 32.5, 0.00],
    [0.01, 32.5, 0.01],
    [0.00, 30.6, 0.00],
    [0.01, 29.6, 0.01],
]

normal_data = np.array(normal_samples)
model = IsolationForest(contamination=0.05, random_state=42)
model.fit(normal_data)

print("隔离森林模型训练完成（基于实际正常数据）")


def calc_trend(history_list):
    if len(history_list) < 5:
        return "stable"

    temps = [x["temperature"] for x in history_list]
    vibs = [x["vibration_magnitude"] for x in history_list]

    temp_delta = temps[-1] - temps[0]
    vib_delta = vibs[-1] - vibs[0]

    # 原来：vib_delta > 50
    # 现在：振动趋势阈值改为 0.05g
    if temp_delta > 1.0 and vib_delta > 0.05:
        return "risk_up"
    elif temp_delta > 1.0:
        return "temperature_up"
    elif vib_delta > 0.05:
        return "vibration_up"
    else:
        return "stable"


def calc_health_score(temp, vibration_magnitude, data_result, trend):
    score = 100.0

    if temp > 25:
        score -= (temp - 25) * 3.0

    # 原来：if vibration_magnitude > 150:
    # 现在：0.15g 以上开始扣分
    if vibration_magnitude > 0.05:
        score -= (vibration_magnitude - 0.05) * 50.0

    if data_result == "data-abnormal":
        score -= 15

    if trend == "temperature_up":
        score -= 5
    elif trend == "vibration_up":
        score -= 8
    elif trend == "risk_up":
        score -= 12

    return max(0, min(100, int(score)))


def analyze_sensor_data(data):
    temp = float(data.get("temperature", 25))
    
    # 单片机已经换算为物理值（g），直接使用
    acc_x = float(data.get("acc_x", 0))
    acc_y = float(data.get("acc_y", 0))
    acc_z = float(data.get("acc_z", 0))
    
    # 合振动加速度（g）
    vibration_magnitude = np.sqrt(acc_x**2 + acc_y**2 + acc_z**2)
    
    # 保存历史
    history.append({
        "temperature": temp,
        "vibration_magnitude": vibration_magnitude
    })
    
    # =========================
    # 阈值判断（物理单位 g）
    # =========================
    VIBRATION_WARN_THRESHOLD = 0.15   # 0.15g 警告
    VIBRATION_CRITICAL_THRESHOLD = 0.30  # 0.30g 严重
    
    # 温度等级
    if temp > TEMP_CRITICAL_THRESHOLD:
        temp_level = "critical"
    elif temp > TEMP_WARN_THRESHOLD:
        temp_level = "warning"
    else:
        temp_level = "normal"
    
    # 振动等级
    if vibration_magnitude > VIBRATION_CRITICAL_THRESHOLD:
        vib_level = "critical"
    elif vibration_magnitude > VIBRATION_WARN_THRESHOLD:
        vib_level = "warning"
    else:
        vib_level = "normal"
    
    # 综合状态
    if temp_level == "critical" or vib_level == "critical":
        if temp_level == "critical" and vib_level == "critical":
            final_status = "critical"
        elif temp_level == "critical":
            final_status = "temp_critical"
        else:
            final_status = "vib_critical"
    elif temp_level == "warning" or vib_level == "warning":
        if temp_level == "warning" and vib_level == "warning":
            final_status = "warning"
        elif temp_level == "warning":
            final_status = "temp_warning"
        else:
            final_status = "vib_warning"
    else:
        final_status = "normal"
    
    # 隔离森林
    features = np.array([[vibration_magnitude, temp, abs(acc_z)]])
    ml_pred = model.predict(features)[0]
    data_result = "data-normal" if ml_pred == 1 else "data-abnormal"
    
    # 趋势
    trend = calc_trend(list(history))
    
    # 健康评分
    health_score = calc_health_score(temp, vibration_magnitude, data_result, trend)
    
    # 动作
    if final_status in ["critical", "temp_critical", "vib_critical"]:
        action = "Stop Now!" if final_status == "critical" else \
                 "Stop! Temp Critical" if final_status == "temp_critical" else \
                 "Stop! Vibration Critical"
    elif final_status in ["warning", "temp_warning", "vib_warning"]:
        action = "Check Soon" if final_status == "warning" else \
                 "Temp Warning" if final_status == "temp_warning" else \
                 "Vibration Warning"
    else:
        action = "Device OK"
    
    return {
        "status": final_status,
        "data": data_result,
        "trend": trend,
        "health_score": health_score,
        "action": action,
        "detail": {
            "temperature": round(temp, 2),
            "vibration_magnitude": round(float(vibration_magnitude), 3),
            "temp_level": temp_level,
            "vib_level": vib_level
        },
        "timestamp": datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    }

# =========================
# MQTT 回调
# =========================
def on_connect(client, userdata, flags, rc):
    print(f"[MQTT] Connected to Broker, rc={rc}")
    if rc == 0:
        client.subscribe(MQTT_TOPIC_SUB)
        print(f"[MQTT] Subscribed: {MQTT_TOPIC_SUB}")
    else:
        print("[MQTT] Connect failed")


def on_message(client, userdata, msg):
    payload = msg.payload.decode(errors="ignore").strip()
    if not payload or payload.startswith("AT") or "MQTTPUBRAW" in payload:
        return
    
    print(f"[MQTT] Raw payload: {payload}")

    # 处理可能粘包
    parts = payload.split("}{")
    for i, part in enumerate(parts):
        if i == 0 and not part.endswith("}"):
            part = part + "}"
        if i == len(parts) - 1 and not part.startswith("{"):
            part = "{" + part
        if not part.startswith("{"):
            part = "{" + part
        if not part.endswith("}"):
            part = part + "}"

        try:
            data = json.loads(part)
            result = analyze_sensor_data(data)

            client.publish(MQTT_TOPIC_PUB, json.dumps(result, ensure_ascii=False))
            print(f"[MQTT] Published: status={result['status']}, data={result['data']}, 温度={data.get('temperature', '?')}°C")

        except json.JSONDecodeError:
            print(f"[MQTT] Skip invalid JSON: {part}")
        except Exception as e:
            print(f"[MQTT] Error: {e}")


def start_mqtt():
    global mqtt_client
    mqtt_client = mqtt.Client(client_id=MQTT_CLIENT_ID, protocol=mqtt.MQTTv311)
    mqtt_client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    mqtt_client.reconnect_delay_set(min_delay=1, max_delay=10)
    mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
    mqtt_client.loop_forever()


@app.route("/")
def index():
    return jsonify({
        "service": "AIoT MQTT Engine",
        "status": "running",
        "model": "IsolationForest",
        "normal_samples": len(normal_samples)
    })


@app.route("/predict", methods=["POST"])
def predict():
    try:
        data = request.json
        result = analyze_sensor_data(data)
        return jsonify(result)
    except Exception as e:
        return jsonify({"error": str(e)}), 500


if __name__ == "__main__":
    mqtt_thread = threading.Thread(target=start_mqtt, daemon=True)
    mqtt_thread.start()
    print("=" * 50)
    print("AI服务已启动（隔离森林 + 阈值判断）")
    print(f"MQTT Broker: {MQTT_BROKER}:{MQTT_PORT}")
    print(f"模型训练样本数: {len(normal_samples)}")
    print("=" * 50)
    app.run(host="0.0.0.0", port=5000, debug=False)
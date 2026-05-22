#include <WiFi.h>
#include <ArduinoJson.h>

// ======== SERVIDOR WEB ========
WiFiServer server(80);

// ======== SSE =========
const int MAX_STREAM_CLIENTS = 4;
WiFiClient streamClients[MAX_STREAM_CLIENTS];

// ======== SERIAL DE RECEPCIÓN ========
HardwareSerial Datos(2);  // RX=16, TX=17
uint32_t irValueGlobal = 0;
int bpmGlobal = 0;
int spo2Global = 0;
int hrFromSpo2Global = 0;

// ======== WEB HTML ========
String webPage() {
  String s = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Monitor Salud</title>
  <style>
    body{
      font-family:Arial;
      background:#2e2e2e;
      text-align:center;
      color:#fff;
    }

    .container{
      display:flex;
      justify-content:center;
      gap:12px;
      flex-wrap:wrap;
      margin-top:10px;
    }

    .card{
      background:#3a3a3a;
      border-radius:10px;
      padding:12px;
      box-shadow:0 2px 8px rgba(0,0,0,0.25);
      width:45%;
      min-width:200px;
    }

    .value{font-size:44px;color:#4aa3ff;margin:6px 0;}
    .value2{font-size:40px;color:#ff6961;margin:6px 0;}

    #plot{
      width:90%;
      height:240px;
      background:#fff;
      border-radius:10px;
      margin:20px auto;
      display:block;
    }

    table{
      margin:0 auto;
      margin-top:20px;
      width:95%;
      max-width:550px;
      border-collapse:collapse;
      background:#3a3a3a;
      color:white;
      border-radius:10px;
      overflow:hidden;
    }
    th,td{
      padding:8px;
      border-bottom:1px solid #555;
    }
    th{
      background:#444;
    }
  </style>
</head>
<body>

  <h1>Monitor en Tiempo Real</h1>

  <!-- TARJETAS -->
  <div class="container">
    <div class="card">
      <div>Frecuencia cardiaca (BPM)</div>
      <div class="value" id="bpm">--</div>
    </div>

    <div class="card">
      <div>Oxigenación (SpO2)</div>
      <div class="value2" id="spo2">--</div>
      <div>HR desde SpO2: <span id="hr_spo2">--</span></div>
    </div>
  </div>

  <!-- GRAFICA -->
  <div>Grafico de BPM</div>
  <canvas id="plot"></canvas>

  <!-- BOTÓN CSV -->
  <button onclick="descargarCSV()" 
  style="margin-top:15px;padding:10px 20px;border:none;border-radius:8px;background:#4aa3ff;color:white;font-size:16px;">
    Descargar CSV
  </button>

  <!-- TABLA -->
  <table>
    <tr>
      <th>Fecha</th>
      <th>BPM</th>
      <th>SpO2</th>
    </tr>
    <tbody id="tablaDatos"></tbody>
  </table>


<script>
  const canvas=document.getElementById('plot');
  const ctx=canvas.getContext('2d');

  function resize(){canvas.width=canvas.clientWidth; canvas.height=240;}
  window.addEventListener('resize',resize); resize();

  const maxPoints = 200;
  let data = new Array(maxPoints).fill(0);

  function draw() {
    ctx.fillStyle = '#fff';
    ctx.fillRect(0,0,canvas.width,canvas.height);

    const base = data[0];
    ctx.strokeStyle = '#e34';
    ctx.beginPath();

    for (let i=0;i<maxPoints;i++){
      let y = canvas.height - ((data[i] - base) / 1500) * canvas.height;
      y = Math.max(0, Math.min(y, canvas.height));

      let x = i*(canvas.width/maxPoints);
      if (i===0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();
  }

  let evtSource = new EventSource('/stream');
  evtSource.onmessage = function(e) {
    const d = JSON.parse(e.data);

    document.getElementById('bpm').innerText = d.bpm>0?d.bpm:'--';
    document.getElementById('spo2').innerText = d.spo2>0?d.spo2+'%':'--';
    document.getElementById('hr_spo2').innerText = d.hr>0?d.hr:'--';


    // ===== FECHA COMPLETA =====
    const fecha = new Date();
    const fechaCompleta = fecha.getFullYear() + "-" +
      String(fecha.getMonth()+1).padStart(2,'0') + "-" +
      String(fecha.getDate()).padStart(2,'0') + " " +
      String(fecha.getHours()).padStart(2,'0') + ":" +
      String(fecha.getMinutes()).padStart(2,'0') + ":" +
      String(fecha.getSeconds()).padStart(2,'0');


    // ===== INSERTAR FILA EN TABLA =====
    const row = `
      <tr>
        <td>${fechaCompleta}</td>
        <td>${d.bpm}</td>
        <td>${d.spo2}</td>
      </tr>`;
    document.getElementById('tablaDatos').insertAdjacentHTML('afterbegin', row);


    // ===== GRAFICA =====
    data.push(d.ir);
    if (data.length>maxPoints) data.shift();
    draw();
  };

  // ===== CSV EXPORT =====
  function descargarCSV(){
    const filas = document.querySelectorAll("#tablaDatos tr");
    let csv = "Fecha,BPM,SpO2\n";

    filas.forEach(fila => {
      const cols = fila.querySelectorAll("td");
      let filaCSV = [];
      cols.forEach(col => filaCSV.push(col.innerText));
      csv += filaCSV.join(",") + "\n";
    });

    const blob = new Blob([csv], { type: "text/csv" });
    const url = window.URL.createObjectURL(blob);

    const a = document.createElement("a");
    a.href = url;
    a.download = "datos_monitor.csv";
    a.click();

    window.URL.revokeObjectURL(url);
  }

</script>

</body>
</html>
)rawliteral";
  return s;
}

// ======== STREAM ========
void addStreamClient(WiFiClient &client) {
  for (int i = 0; i < MAX_STREAM_CLIENTS; ++i) {
    if (!streamClients[i] || !streamClients[i].connected()) {
      streamClients[i] = client;
      streamClients[i].println("HTTP/1.1 200 OK");
      streamClients[i].println("Content-Type: text/event-stream");
      streamClients[i].println("Cache-Control: no-cache");
      streamClients[i].println("Connection: keep-alive");
      streamClients[i].println();
      return;
    }
  }
  client.stop();
}

void broadcastStream(uint32_t irv, int bpm, int spo2, int hr_spo2) {
  String payload = 
    "{\"ir\":" + String(irv) +
    ",\"bpm\":" + String(bpm) +
    ",\"spo2\":" + String(spo2) +
    ",\"hr\":" + String(hr_spo2) + "}";

  for (int i = 0; i < MAX_STREAM_CLIENTS; i++) {
    if (streamClients[i] && streamClients[i].connected()) {
      streamClients[i].print("data: ");
      streamClients[i].println(payload);
      streamClients[i].println();
    } else {
      streamClients[i].stop();
      streamClients[i] = WiFiClient();
    }
  }
}

// ======== HTTP ========
String readRequestLine(WiFiClient &client, unsigned long timeout = 1000) {
  String line = "";
  unsigned long start = millis();
  while ((millis() - start) < timeout) {
    if (client.available()) {
      line = client.readStringUntil('\n');
      break;
    }
  }
  return line;
}

// ======================
// SETUP
// ======================
void setup() {
  Serial.begin(115200);
  Datos.begin(115200, SERIAL_8N1, 16, 17);

  delay(20);

  WiFi.softAP("ESP32-WebServer", "12345678");
  Serial.print("IP AP: ");
  Serial.println(WiFi.softAPIP());

  server.begin();
}

// ======================
// LOOP
// ======================
void loop() {

  // ======== LEER SERIAL JSON ========
  while (Datos.available()) {
    String linea = Datos.readStringUntil('\n');
    linea.trim();
    if (linea.length() == 0) continue;

    DynamicJsonDocument doc(200);
    DeserializationError err = deserializeJson(doc, linea);

    if (!err) {
      irValueGlobal      = doc["ir"]  | irValueGlobal;
      bpmGlobal          = doc["bpm"] | bpmGlobal;
      spo2Global         = doc["spo2"] | spo2Global;
      hrFromSpo2Global   = doc["hr"] | hrFromSpo2Global;
    }
  }

  // ======== HTTP / STREAM ========
  WiFiClient client = server.available();
  if (client) {
    String reqLine = readRequestLine(client);

    if (reqLine.indexOf("GET /stream") >= 0) {
      while (client.available()) {
        String h = client.readStringUntil('\n');
        if (h == "\r") break;
      }
      addStreamClient(client);
    } else {
      while (client.available()) {
        String h = client.readStringUntil('\n');
        if (h == "\r") break;
      }
      client.println("HTTP/1.1 200 OK");
      client.println("Content-type:text/html");
      client.println("Connection: close");
      client.println();
      client.print(webPage());
      client.stop();
    }
  }

  // ======== BROADCAST SSE ========
  broadcastStream(irValueGlobal, bpmGlobal, spo2Global, hrFromSpo2Global);

  delay(50);  // cada 5 segundos
}





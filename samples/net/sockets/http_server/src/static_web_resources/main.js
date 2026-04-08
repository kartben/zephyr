/*
 * Copyright (c) 2024, Witekio
 *
 * SPDX-License-Identifier: Apache-2.0
 */

const THROUGHPUT_DURATION_MS = 10000;
const RATE_BAR_CAP_MBIT = 100;
const UI_THROTTLE_MS = 50;

/* Cached DOM references — resolved once after DOMContentLoaded */
let uptimeEl, speedEl, totalEl, timeFill, timeMeter, throughputBtn;

const NET_STAT_IDS = [
	"bytes_recv", "bytes_sent",
	"ipv6_pkt_recv", "ipv6_pkt_sent",
	"ipv4_pkt_recv", "ipv4_pkt_sent",
	"tcp_bytes_recv", "tcp_bytes_sent",
];
const netStatEls = {};

function formatMiB(bytes) {
	return (bytes / (1024 * 1024)).toFixed(2);
}

function bytesPerSecToMbitPerSec(bytesPerSec) {
	return (bytesPerSec * 8) / 1e6;
}

async function fetchUptime() {
	try {
		const response = await fetch("/uptime");
		if (!response.ok) throw new Error(`Response status: ${response.status}`);
		uptimeEl.innerHTML = "Uptime: " + await response.json() + " milliseconds";
	} catch (error) {
		console.error(error.message);
	}
}

async function postLed(state) {
	try {
		const response = await fetch("/led", {
			method: "POST",
			body: JSON.stringify({ led_num: 0, led_state: state }),
		});
		if (!response.ok) throw new Error(`Response status: ${response.status}`);
	} catch (error) {
		console.error(error.message);
	}
}

async function runThroughputTest() {
	throughputBtn.disabled = true;
	speedEl.textContent = "Starting…";
	totalEl.textContent = "Total received: 0.00 MiB";
	timeFill.style.width = "0%";
	timeMeter.setAttribute("aria-valuenow", "0");

	const t0 = performance.now();
	let bytesReceived = 0;
	let lastUi = t0;
	/* Instantaneous rate window — reset at loop entry, not at t0 */
	let lastInstBytes = 0;
	let lastInstTime = t0;

	try {
		const response = await fetch("/throughput");
		if (!response.ok) throw new Error(`HTTP ${response.status}`);
		if (!response.body) throw new Error("No response body (streaming not supported)");

		const reader = response.body.getReader();

		for (;;) {
			const { done, value } = await reader.read();
			if (value?.length > 0) bytesReceived += value.length;

			const now = performance.now();
			if (!done && now - lastUi < UI_THROTTLE_MS) continue;
			lastUi = now;

			const elapsedSec = (now - t0) / 1000;
			const avgBps = elapsedSec > 0 ? bytesReceived / elapsedSec : 0;

			const sliceSec = (now - lastInstTime) / 1000;
			const instBps = sliceSec > 0 ? (bytesReceived - lastInstBytes) / sliceSec : 0;
			lastInstBytes = bytesReceived;
			lastInstTime = now;

			speedEl.textContent =
				bytesPerSecToMbitPerSec(instBps).toFixed(2) + " Mbit/s (instant) · " +
				bytesPerSecToMbitPerSec(avgBps).toFixed(2) + " Mbit/s (avg)";
			totalEl.textContent = "Total received: " + formatMiB(bytesReceived) + " MiB";

			const timePct = Math.min(100, ((now - t0) / THROUGHPUT_DURATION_MS) * 100);
			timeFill.style.width = timePct + "%";
			timeMeter.setAttribute("aria-valuenow", String(Math.round(timePct)));

			if (done) break;
		}
	} catch (error) {
		console.error(error.message);
		speedEl.textContent = "Error: " + error.message;
	} finally {
		throughputBtn.disabled = false;
	}
}

window.addEventListener("DOMContentLoaded", () => {
	/* Cache DOM references */
	uptimeEl = document.getElementById("uptime");
	speedEl = document.getElementById("throughput_speed");
	totalEl = document.getElementById("throughput_total");
	timeFill = document.getElementById("throughput_time_fill");
	timeMeter = document.getElementById("throughput_time_meter");
	throughputBtn = document.getElementById("throughput_start");

	for (const id of NET_STAT_IDS) {
		netStatEls[id] = document.getElementById(id);
	}

	setInterval(fetchUptime, 1000);

	document.getElementById("led_on").addEventListener("click", () => postLed(true));
	document.getElementById("led_off").addEventListener("click", () => postLed(false));
	throughputBtn.addEventListener("click", runThroughputTest);

	/* WebSocket on "/" — same origin; browser upgrades HTTP→WS automatically */
	const ws = new WebSocket("/");
	ws.onmessage = (event) => {
		const data = JSON.parse(event.data);
		for (const id of NET_STAT_IDS) {
			netStatEls[id].innerHTML = data[id];
		}
	};
});
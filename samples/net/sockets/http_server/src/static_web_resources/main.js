/*
 * Copyright (c) 2024, Witekio
 *
 * SPDX-License-Identifier: Apache-2.0
 */
async function fetchUptime()
{
	try {
		const response = await fetch("/uptime");
		if (!response.ok) {
			throw new Error(`Response status: ${response.status}`);
		}

		const json = await response.json();
		const uptime = document.getElementById("uptime");
		uptime.innerHTML = "Uptime: " + json + " milliseconds"
	} catch (error) {
		console.error(error.message);
	}
}

async function postLed(state)
{
	try {
		const payload = JSON.stringify({"led_num" : 0, "led_state" : state});

		const response = await fetch("/led", {method : "POST", body : payload});
		if (!response.ok) {
			throw new Error(`Response satus: ${response.status}`);
		}
	} catch (error) {
		console.error(error.message);
	}
}

function setNetStat(json_data, stat_name)
{
	document.getElementById(stat_name).innerHTML = json_data[stat_name];
}

const THROUGHPUT_DURATION_MS = 10000;
const RATE_BAR_CAP_MBIT = 100;
const UI_THROTTLE_MS = 50;

function formatMiB(bytes)
{
	return (bytes / (1024 * 1024)).toFixed(2);
}

function bytesPerSecToMbitPerSec(bytesPerSec)
{
	return (bytesPerSec * 8) / 1e6;
}

async function runThroughputTest()
{
	const btn = document.getElementById("throughput_start");
	const speedEl = document.getElementById("throughput_speed");
	const totalEl = document.getElementById("throughput_total");
	const timeFill = document.getElementById("throughput_time_fill");
	const rateFill = document.getElementById("throughput_rate_fill");
	const timeMeter = document.getElementById("throughput_time_meter");

	btn.disabled = true;
	speedEl.textContent = "Starting…";
	totalEl.textContent = "Total received: 0.00 MiB";
	timeFill.style.width = "0%";
	rateFill.style.width = "0%";
	timeMeter.setAttribute("aria-valuenow", "0");

	const t0 = performance.now();
	let bytesReceived = 0;
	let lastUi = t0;
	let lastBytesForInst = 0;
	let lastInstTime = t0;

	try {
		const response = await fetch("/throughput");
		if (!response.ok) {
			throw new Error(`HTTP ${response.status}`);
		}
		if (!response.body) {
			throw new Error("No response body (streaming not supported)");
		}

		const reader = response.body.getReader();

		for (;;) {
			const { done, value } = await reader.read();
			if (value && value.length > 0) {
				bytesReceived += value.length;
			}

			const now = performance.now();
			const shouldPaint = done || (now - lastUi >= UI_THROTTLE_MS);

			if (!shouldPaint) {
				continue;
			}

			lastUi = now;

			const elapsedMs = now - t0;
			const elapsedSec = elapsedMs / 1000;
			const avgBps = elapsedSec > 0 ? bytesReceived / elapsedSec : 0;
			const sliceSec = (now - lastInstTime) / 1000;
			const instBps = sliceSec > 0 ? (bytesReceived - lastBytesForInst) / sliceSec : 0;

			lastBytesForInst = bytesReceived;
			lastInstTime = now;

			const instMbit = bytesPerSecToMbitPerSec(instBps);
			const avgMbit = bytesPerSecToMbitPerSec(avgBps);

			speedEl.textContent = instMbit.toFixed(2) + " Mbit/s (instant) · " + avgMbit.toFixed(2) + " Mbit/s (avg)";
			totalEl.textContent = "Total received: " + formatMiB(bytesReceived) + " MiB";

			const timePct = Math.min(100, (elapsedMs / THROUGHPUT_DURATION_MS) * 100);
			timeFill.style.width = timePct + "%";
			timeMeter.setAttribute("aria-valuenow", String(Math.round(timePct)));

			const ratePct = Math.min(100, (instMbit / RATE_BAR_CAP_MBIT) * 100);
			rateFill.style.width = ratePct + "%";

			if (done) {
				break;
			}
		}

	} catch (error) {
		console.error(error.message);
		speedEl.textContent = "Error: " + error.message;
	} finally {
		btn.disabled = false;
	}
}

window.addEventListener("DOMContentLoaded", (ev) => {
	/* Fetch the uptime once per second */
	setInterval(fetchUptime, 1000);

	/* POST to the LED endpoint when the buttons are pressed */
	const led_on_btn = document.getElementById("led_on");
	led_on_btn.addEventListener("click", (event) => {
		console.log("led_on clicked");
		postLed(true);
	})

	const led_off_btn = document.getElementById("led_off");
	led_off_btn.addEventListener("click", (event) => {
		console.log("led_off clicked");
		postLed(false);
	})

	const throughputBtn = document.getElementById("throughput_start");
	throughputBtn.addEventListener("click", () => {
		runThroughputTest();
	});

	/* Setup websocket for handling network stats */
	const ws = new WebSocket("/");

	ws.onmessage = (event) => {
		const data = JSON.parse(event.data);
		setNetStat(data, "bytes_recv");
		setNetStat(data, "bytes_sent");
		setNetStat(data, "ipv6_pkt_recv");
		setNetStat(data, "ipv6_pkt_sent");
		setNetStat(data, "ipv4_pkt_recv");
		setNetStat(data, "ipv4_pkt_sent");
		setNetStat(data, "tcp_bytes_recv");
		setNetStat(data, "tcp_bytes_sent");
	}
})

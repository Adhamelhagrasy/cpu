const scheduler = document.getElementById("scheduler");
const quantumField = document.getElementById("quantum-field");
const programList = document.getElementById("program-list");
const programCount = document.getElementById("program-count");
const output = document.getElementById("output");
const outputMeta = document.getElementById("output-meta");
const statusPill = document.getElementById("status-pill");
const runButton = document.getElementById("run-button");
const promptForm = document.getElementById("prompt-form");
const promptLabel = document.getElementById("prompt-label");
const promptInput = document.getElementById("prompt-input");
const submitInputButton = document.getElementById("submit-input");
const addProgramButtons = [
    document.getElementById("add-program"),
    document.getElementById("add-program-inline")
];

let programs = [{ filename: "Program_1.txt", arrivalTime: 0 }];
let activeSessionId = null;

function setStatus(text, className = "") {
    statusPill.textContent = text;
    statusPill.className = `status-pill ${className}`.trim();
}

function setPromptVisible(visible, label = "Runtime input") {
    promptForm.classList.toggle("hidden", !visible);
    promptLabel.textContent = label;
    if (visible) {
        promptInput.value = "";
        promptInput.focus();
    }
}

function setRunControlsDisabled(disabled) {
    runButton.disabled = disabled;
    addProgramButtons.forEach((button) => {
        button.disabled = disabled;
    });
    document.querySelectorAll(".program-file, .program-arrival, #scheduler, #quantum, .remove-program")
        .forEach((element) => {
            element.disabled = disabled;
        });
}

function updateProgramCount() {
    const count = programs.length;
    programCount.textContent = `${count} program${count === 1 ? "" : "s"}`;
}

function renderPrograms() {
    programList.innerHTML = "";

    programs.forEach((program, index) => {
        const row = document.createElement("div");
        row.className = "program-row";
        row.innerHTML = `
            <label class="field">
                <span>Filename</span>
                <input class="program-file" type="text" placeholder="Program_1.txt" value="${program.filename}">
            </label>
            <label class="field narrow">
                <span>Arrival</span>
                <input class="program-arrival" type="number" min="0" value="${program.arrivalTime}">
            </label>
            <button class="danger-button remove-program" type="button">Remove</button>
        `;

        const fileInput = row.querySelector(".program-file");
        const arrivalInput = row.querySelector(".program-arrival");
        const removeButton = row.querySelector(".remove-program");

        fileInput.addEventListener("input", (event) => {
            programs[index].filename = event.target.value;
        });

        arrivalInput.addEventListener("input", (event) => {
            programs[index].arrivalTime = Number(event.target.value || 0);
        });

        removeButton.addEventListener("click", () => {
            programs.splice(index, 1);
            if (!programs.length) {
                programs = [{ filename: "", arrivalTime: 0 }];
            }
            renderPrograms();
        });

        programList.appendChild(row);
    });

    updateProgramCount();
}

function addProgramRow(file = "", arrival = 0) {
    programs.push({ filename: file, arrivalTime: arrival });
    renderPrograms();
}

function toggleQuantum() {
    quantumField.style.display = scheduler.value === "RR" ? "flex" : "none";
}

function collectPrograms() {
    return programs
        .map((entry) => ({
            filename: entry.filename.trim(),
            arrivalTime: Number(entry.arrivalTime || 0)
        }))
        .filter((entry) => entry.filename);
}

function loadSamples() {
    programs = [
        { filename: "Program_1.txt", arrivalTime: 0 },
        { filename: "Program_2.txt", arrivalTime: 1 },
        { filename: "Program_3.txt", arrivalTime: 4 }
    ];
    renderPrograms();
}

addProgramButtons.forEach((button) => {
    button.addEventListener("click", () => addProgramRow());
});
document.getElementById("load-samples").addEventListener("click", loadSamples);
scheduler.addEventListener("change", toggleQuantum);

document.getElementById("run-form").addEventListener("submit", async (event) => {
    event.preventDefault();

    const programs = collectPrograms();
    if (!programs.length) {
        setStatus("Error", "error");
        output.textContent = "Add at least one program file before running the simulation.";
        return;
    }

    const payload = {
        scheduler: scheduler.value,
        quantum: Number(document.getElementById("quantum").value || 2),
        programs
    };

    setRunControlsDisabled(true);
    setPromptVisible(false);
    setStatus("Running", "running");
    outputMeta.textContent = `Launching ${payload.scheduler} with ${programs.length} program(s).`;
    output.textContent = "Running simulator...";
    activeSessionId = null;

    try {
        const response = await fetch("/api/start", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload)
        });
        const result = await response.json();

        if (!response.ok || !result.ok) {
            throw new Error(result.error || "Simulation failed.");
        }

        activeSessionId = result.sessionId || null;
        outputMeta.textContent = result.summary;
        output.textContent = result.output || "(No stdout produced)";

        if (result.waitingForInput) {
            setStatus("Waiting");
            setPromptVisible(true, result.prompt || "Runtime input");
        } else if (result.completed) {
            setStatus("Complete");
            setRunControlsDisabled(false);
        } else {
            setStatus("Running", "running");
        }
    } catch (error) {
        setStatus("Error", "error");
        outputMeta.textContent = "The backend could not complete the run.";
        output.textContent = error.message;
        setRunControlsDisabled(false);
    }
});

promptForm.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!activeSessionId) return;

    submitInputButton.disabled = true;
    setStatus("Running", "running");

    try {
        const response = await fetch("/api/input", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                sessionId: activeSessionId,
                input: promptInput.value
            })
        });
        const result = await response.json();

        if (!response.ok || !result.ok) {
            throw new Error(result.error || "Input submission failed.");
        }

        outputMeta.textContent = result.summary;
        output.textContent = result.output || "(No stdout produced)";

        if (result.waitingForInput) {
            setStatus("Waiting");
            setPromptVisible(true, result.prompt || "Runtime input");
        } else if (result.completed) {
            activeSessionId = null;
            setStatus("Complete");
            setPromptVisible(false);
            setRunControlsDisabled(false);
        } else {
            setStatus("Running", "running");
            setPromptVisible(false);
        }
    } catch (error) {
        setStatus("Error", "error");
        outputMeta.textContent = "The backend could not continue the run.";
        output.textContent = error.message;
        setPromptVisible(false);
        setRunControlsDisabled(false);
        activeSessionId = null;
    } finally {
        submitInputButton.disabled = false;
    }
});

renderPrograms();
toggleQuantum();
setStatus("Idle");
setPromptVisible(false);

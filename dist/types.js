export var Flag;
(function (Flag) {
    Flag[Flag["Voltage"] = 1] = "Voltage";
    Flag[Flag["VoltageSag"] = 2] = "VoltageSag";
    Flag[Flag["VoltageSwell"] = 4] = "VoltageSwell";
    Flag[Flag["Current"] = 8] = "Current";
    Flag[Flag["Frequency"] = 16] = "Frequency";
    Flag[Flag["Anomaly"] = 32] = "Anomaly";
    Flag[Flag["Clipped"] = 64] = "Clipped";
    Flag[Flag["DataGap"] = 128] = "DataGap";
})(Flag || (Flag = {}));

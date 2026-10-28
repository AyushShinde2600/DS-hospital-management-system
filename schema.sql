CREATE DATABASE IF NOT EXISTS hospital_management;
USE hospital_management;

CREATE TABLE IF NOT EXISTS patients (
    patient_id INT PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    age INT NOT NULL,
    gender VARCHAR(20),
    contact VARCHAR(20),
    symptoms VARCHAR(200),
    emergency_level INT CHECK (emergency_level BETWEEN 1 AND 4),
    registration_time DATETIME,
    status VARCHAR(30)
);

CREATE TABLE IF NOT EXISTS medical_history (
    history_id INT AUTO_INCREMENT PRIMARY KEY,
    patient_id INT,
    action VARCHAR(100),
    description VARCHAR(255),
    action_time DATETIME,
    FOREIGN KEY (patient_id) REFERENCES patients(patient_id) ON DELETE CASCADE
);
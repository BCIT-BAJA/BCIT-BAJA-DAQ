
# Problem 
During the last BAJA competition,The BAJA team was unable to measure dynamic forces applied onto the suspension of the car. As a result, during the last competition, it bent.

<img width="530" height="384" alt="image" src="https://github.com/user-attachments/assets/10aa4100-72ae-4112-be75-6f655a311f51" />

The telemetry system for BCIT-BAJA car, collects strain across suspension components of car to allow BAJA to optimize material and reinforce weak areas of suspension.
As well as collect other data such as linear/angular acceleration.

There consists of 2 parts of this project. 

**PART B** the amplification of the (-15-15mV) analog strain signal to (0-3.0V) which is then routed to

**PART A**, which collects the 8 conditioned analog signals and is transmitted/received over a 915MHz antenna



# MAINBOARD( PART A)

Power supply circuitry
<img width="975" height="389" alt="image" src="https://github.com/user-attachments/assets/051b7de4-688c-4ae6-ad54-d9bb7aa79901" />

Conditioning of analog signals 
<img width="975" height="567" alt="image" src="https://github.com/user-attachments/assets/a6cae0f2-0252-4850-a128-f059bdfef1b9" />

Additional peripherals used
<img width="975" height="1148" alt="image" src="https://github.com/user-attachments/assets/9defa2b3-a4df-446f-8e07-2e66edd7477b" />


# Amplifier circuit ( PART B) - 
<img width="975" height="489" alt="image" src="https://github.com/user-attachments/assets/c5423ab0-2987-4b5f-ba64-87e19df32614" />

Example of LTSPICE simulation used for amplifier board + signal conditioning

<img width="975" height="804" alt="image" src="https://github.com/user-attachments/assets/86b31d29-71cc-47ab-8f6d-3be30591c29b" />

Verified amplifier board working
<img width="975" height="567" alt="image" src="https://github.com/user-attachments/assets/97f6bca2-eba5-471d-8e2b-93cc97f4937b" />

# Results of real-world appied force vs output voltage 
<img width="1356" height="620" alt="image" src="https://github.com/user-attachments/assets/b343138e-8436-4540-9d79-35ff9e8cacf9" />

// 

#pragma once

#ifdef Protocol_Implmentation
#endif

// 
// Remote
// Hub
// Host
// 
// Each has a state machine.
// 

// Pull-Protocol (Call/Response)
//
// Remote
// // Version
// // Status/State
// // Uptime
// // Connection Time
// // Connection Strength
//
// Hub
// // Version
// // Status/State
// // Uptime
// // Connection Time
// // Connection Strength
// 
// (User Interface)
// Host
// // Version
// // Status/State
// // Uptime
// // Connection Time
// // Connection Strength
//

// todo: make sense of data bandwidth, latency, jitter, alerts 

// Push-Protocol <<------- todo
//
// Remote
// // Data Packet
// Hub
// // (Remote)
// // Alert Packet -- State Change


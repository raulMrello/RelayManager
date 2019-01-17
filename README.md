# RelayManager

RelayManager is a component that controls a group of Relay drivers [Relay](https://github.com/raulMrello/Driver_Relay)

It is an Active Object, ```arm mbed``` compatible that includes a Hierarchical state machine [StateMachine](https://github.com/raulMrello/StateMachine). It also, uses [MQLib](https://github.com/raulMrello/MQLib) as pub-sub infrastructure to communicate with other components in a decoupled way.

It can be accessed through different topic updates, using binary data structures (blob). These struct definitions are included in file ```RelayManagerBlob.h```, so other componentes can include this file, to communicate with it.



  
## Changelog

---
### **17.01.2019**
- [x] Initial commit
## Offline Emergency Mesh — Simulation Prototype

An offline-first emergency communication prototype built with ESP32-S3. The current simulation demonstrates a portable victim node with a symbol-based OLED interface for sending SOS, medical, trapped, safe, fire, water, and route-blocked alerts without language barriers.

Alerts are designed to use ESP-NOW, a low-power peer-to-peer protocol that works without internet, cellular service, or a Wi-Fi router. The intended system connects victim nodes to a parent gateway node, allowing emergency alerts to be monitored and coordination responses such as “help dispatched” to be sent back.

This repository currently contains the Wokwi simulation for the node interface, alert workflow, LED/buzzer feedback, and ESP-NOW-ready packet logic. Future updates will add real multi-node hardware testing, gateway-to-computer monitoring, offline message storage, acknowledgements, retries, and relay-based self-healing mesh functionality.

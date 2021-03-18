# COMP3331 ASSIGNMENT  

For this assignment, you will be asked to implement a part of the peer-to-peer (P2P) 
protocol Circular DHT which is described in Section 2.6 of the text Computer 
Networking (6th ed) and would be discussed in the lecture. A primary requirement for 
a P2P application is that the peers form a connected network at all time. A complication 
that a P2P network must be able to deal with is that peers can join and leave the 
network at any time. For example, if a peer leaves the network suddenly (because it 
has crashed), then the remaining peers must try to keep a connected network without 
this peer. It is therefore necessary to design P2P networks so that they can deal with 
these complications. One such method has been described in Section 2.6 of the text, 
under the heading of Peer Churn for circular DHT. You will also learn how to implement 
reliable data transmission over UDP.

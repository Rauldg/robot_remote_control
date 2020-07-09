#include "RobotController.hpp"
#include <iostream>
#include <memory>

using namespace robot_remote_control;


RobotController::RobotController(TransportSharedPtr commandTransport,TransportSharedPtr telemetryTransport, size_t buffersize):UpdateThread(),
    commandTransport(commandTransport),
    telemetryTransport(telemetryTransport),
    buffers(std::make_shared<TelemetryBuffer>()) {

    simplesensorbuffer = std::shared_ptr<SimpleBuffer<SimpleSensor> >(new SimpleBuffer<SimpleSensor>());

    registerTelemetryType<Pose>(CURRENT_POSE, buffersize);
    registerTelemetryType<JointState>(JOINT_STATE, buffersize);
    registerTelemetryType<JointState>(CONTROLLABLE_JOINTS, buffersize);
    registerTelemetryType<SimpleActions>(SIMPLE_ACTIONS, buffersize);
    registerTelemetryType<ComplexActions>(COMPLEX_ACTIONS, buffersize);
    registerTelemetryType<RobotName>(ROBOT_NAME, buffersize);
    registerTelemetryType<RobotState>(ROBOT_STATE, buffersize);
    registerTelemetryType<LogMessage>(LOG_MESSAGE, buffersize);
    registerTelemetryType<VideoStreams>(VIDEO_STREAMS, buffersize);
    registerTelemetryType<SimpleSensors>(SIMPLE_SENSOR_DEFINITION, buffersize);
    // simple sensors are stored in separate buffer when receiving, but sending requires this for requests
    //registerTelemetryType<SimpleSensor>(SIMPLE_SENSOR_VALUE, buffersize);
    registerTelemetryType<WrenchState>(WRENCH_STATE, buffersize);
}

RobotController::~RobotController() {
}

void RobotController::setTargetPose(const Pose & pose) {
    sendProtobufData(pose, TARGET_POSE_COMMAND);
}

void RobotController::setTwistCommand(const Twist &twistCommand) {
    sendProtobufData(twistCommand, TWIST_COMMAND);
}

void RobotController::setGoToCommand(const GoTo &goToCommand) {
    sendProtobufData(goToCommand, GOTO_COMMAND);
}

void RobotController::setJointCommand(const JointState &jointsCommand) {
    sendProtobufData(jointsCommand, JOINTS_COMMAND);
}

void RobotController::setSimpleActionCommand(const SimpleAction &simpleActionCommand) {
    SimpleActions action;
    (*action.add_actions()) = simpleActionCommand;
    sendProtobufData(action, SIMPLE_ACTIONS_COMMAND);
}

void RobotController::setComplexActionCommand(const ComplexAction &complexActionCommand) {
    sendProtobufData(complexActionCommand, COMPLEX_ACTION_COMMAND);
}

void RobotController::setLogLevel(const uint32_t &level) {
    std::string buf;
    buf.resize(sizeof(uint16_t) + sizeof(uint32_t));
    uint16_t* data = reinterpret_cast<uint16_t*>(const_cast<char*>(buf.data()));
    *data = LOG_LEVEL_SELECT;

    uint32_t *levelptr = reinterpret_cast<uint32_t*>(const_cast<char*>(buf.data()+sizeof(uint16_t)));
    *levelptr = level;
    sendRequest(buf);
}

void RobotController::update() {
    if (telemetryTransport.get()) {
        std::string buf;
        Transport::Flags flags = Transport::NONE;
        // if (!this->threaded()){
            flags = Transport::NOBLOCK;
        // }
        while (telemetryTransport->receive(&buf, flags)) {
            evaluateTelemetry(buf);
        }
    } else {
        printf("ERROR no telemetry Transport set\n");
    }
}


std::string RobotController::sendRequest(const std::string& serializedMessage) {
    commandTransport->send(serializedMessage);
    std::string replystr;

    // blocking receive
    while (commandTransport->receive(&replystr) == 0) {
        // wait time depends on how long the transports recv blocks
    }

    return replystr;
}

TelemetryMessageType RobotController::evaluateTelemetry(const std::string& reply) {
    uint16_t* type = reinterpret_cast<uint16_t*>(const_cast<char*>(reply.data()));

    std::string serializedMessage(reply.data()+sizeof(uint16_t), reply.size()-sizeof(uint16_t));

    TelemetryMessageType msgtype = (TelemetryMessageType)*type;

    // try to resolve through registered types
    std::shared_ptr<TelemetryAdderBase> adder = telemetryAdders[msgtype];
    if (adder.get()) {
        adder->addToTelemetryBuffer(msgtype, serializedMessage);
        return msgtype;
    }

    // handle special types

    switch (msgtype) {
        // multi values in single stream
        case SIMPLE_SENSOR_VALUE:       addToSimpleSensorBuffer(serializedMessage);
                                        return msgtype;

        case TELEMETRY_MESSAGE_TYPES_NUMBER:
        case NO_TELEMETRY_DATA:
        {
            return msgtype;
        }
        default: return msgtype;
    }

    // should never reach this
    return NO_TELEMETRY_DATA;
}

void RobotController::addToSimpleSensorBuffer(const std::string &serializedMessage) {
    SimpleSensor data;
    data.ParseFromString(serializedMessage);
    // check if buffer number is big enough
    // size must be id+1 (id 0 needs size 1)
    simplesensorbuffer->initBufferID(data.id());
    // if (simplesensorbuffer->size() <= data.id()){
    //     simplesensorbuffer->resize(data.id());
    // }

    simplesensorbuffer->lock();
    RingBufferAccess::pushData(simplesensorbuffer->get_ref()[data.id()], data, true);
    simplesensorbuffer->unlock();

}
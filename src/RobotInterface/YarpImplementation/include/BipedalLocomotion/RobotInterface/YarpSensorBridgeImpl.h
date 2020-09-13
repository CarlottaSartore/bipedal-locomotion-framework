/**
 * @file YarpSensorBridgeImpl.h
 * @authors Prashanth Ramadoss
 * @copyright 2020 Istituto Italiano di Tecnologia (IIT). This software may be modified and
 * distributed under the terms of the GNU Lesser General Public License v2.1 or any later version.
 */

#ifndef BIPEDAL_LOCOMOTION_ROBOT_INTERFACE_YARP_SENSOR_BRIDGE_IMPL_H
#define BIPEDAL_LOCOMOTION_ROBOT_INTERFACE_YARP_SENSOR_BRIDGE_IMPL_H

#include <BipedalLocomotion/RobotInterface/YarpSensorBridge.h>
#include <BipedalLocomotion/GenericContainer/Vector.h>

// YARP Sensor Interfaces
#include <yarp/dev/IAnalogSensor.h>
#include <yarp/dev/IGenericSensor.h>
#include <yarp/dev/MultipleAnalogSensorsInterfaces.h>

// YARP Camera Interfaces
#include <yarp/dev/FrameGrabberInterfaces.h>
#include <yarp/dev/IRGBDSensor.h>

// YARP Control Board Interfaces
#include <yarp/dev/IEncodersTimed.h>
#include <yarp/dev/IAxisInfo.h>

// std
#include <set>
#include <algorithm>

using namespace BipedalLocomotion::RobotInterface;
using namespace BipedalLocomotion::GenericContainer;

namespace BipedalLocomotion
{
namespace RobotInterface
{

struct YarpSensorBridge::Impl
{
    /**
     * Struct holding remapped remote control board interfaces
     */
    struct
    {
        yarp::dev::IEncodersTimed* encoders;
        yarp::dev::IAxisInfo* axis;
    } controlBoardRemapperInterfaces;

    /**
     * Struct holding remapped MAS interfaces -inertial sensors related
     */
    struct
    {
        yarp::dev::IThreeAxisLinearAccelerometers* accelerometers;
        yarp::dev::IThreeAxisGyroscopes* gyroscopes;
        yarp::dev::IThreeAxisMagnetometers* magnetometers;
        yarp::dev::IOrientationSensors* orientationSensors;
    } wholeBodyMASInertialsInterface;

    /**
     * Struct holding remapped MAS interfaces - FT sensors related
     */
    struct
    {
        yarp::dev::ISixAxisForceTorqueSensors* sixAxisFTSensors;
    } wholeBodyMASForceTorquesInterface;

    std::unordered_map<std::string, yarp::dev::IGenericSensor*> wholeBodyAnalogIMUInterface; /** < map  of IMU sensors attached through generic sensor interfaces */
    std::unordered_map<std::string, yarp::dev::IGenericSensor*> wholeBodyCartesianWrenchInterface; /** < map  of cartesian wrench streams attached through generic sensor interfaces */
    std::unordered_map<std::string, yarp::dev::IAnalogSensor*> wholeBodyAnalogSixAxisFTSensorsInterface; /** < map  of six axis force torque sensors attached through analog sensor interfaces */
    std::unordered_map<std::string, yarp::dev::IFrameGrabberImage*> wholeBodyFrameGrabberInterface; /** < map of cameras attached through frame grabber interfaces */
    std::unordered_map<std::string, yarp::dev::IRGBDSensor*> wholeBodyRGBDInterface; /** < map of cameras attached through RGBD interfaces */

    /**
     * Struct holding measurements polled from remapped remote control board interfaces
     */
    struct
    {
        Eigen::VectorXd remappedJointIndices;
        Eigen::VectorXd jointPositions;
        Eigen::VectorXd jointVelocities;
        double receivedTimeInSeconds;
    } controlBoardRemapperMeasures;

    std::unordered_map<std::string, Vector12d> wholeBodyIMUMeasures; /** < map holding analog IMU sensor measurements */
    std::unordered_map<std::string, Vector6d> wholeBodyFTMeasures; /** < map holding six axis force torque measures */
    std::unordered_map<std::string, Eigen::Vector3d> wholeBodyInertialMeasures; /** < map holding three axis inertial sensor measures */
    std::unordered_map<std::string, Vector6d> wholeBodyCartesianWrenchMeasures; /** < map holding cartesian wrench measures */
    std::unordered_map<std::string, Eigen::MatrixXd> wholeBodyCameraImages; /** < map holding images **/

    const int nrChannelsInYARPGenericIMUSensor{12};
    const int nrChannelsInYARPGenericCartesianWrench{6};
    const int nrChannelsInYARPAnalogSixAxisFTSensor{6};

    SensorBridgeMetaData metaData; /**< struct holding meta data **/
    bool bridgeInitialized{false}; /**< flag set to true if the bridge is successfully initialized */
    bool driversAttached{false}; /**< flag set to true if the bridge is successfully attached to required device drivers */
    bool sensorDryRunEnabled{false}; /**< flag to enable running a test stream of sensor interfaces after attaching to the device drivers */


    using SubConfigLoader = bool (YarpSensorBridge::Impl::*)(std::weak_ptr<IParametersHandler>,
                                                             SensorBridgeMetaData&);

    /**
     * Checks is a stream is enabled in configuration and
     * loads the relevant stream group from configuration
     */
    bool subConfigLoader(const std::string& enableStreamString,
                         const std::string& streamGroupString,
                         const SubConfigLoader loader,
                         std::weak_ptr<IParametersHandler> handler,
                         SensorBridgeMetaData& metaData,
                         bool& enableStreamFlag)
    {
        constexpr std::string_view logPrefix = "[YarpSensorBridge::Impl::subConfigLoader] ";

        auto ptr = handler.lock();
        if (ptr == nullptr)
        {
            std::cerr << logPrefix << "The handler is not pointing to an already initialized memory."
                      << std ::endl;
            return false;
        }

        int success;
        if (ptr->getParameter(enableStreamString, success))
        {
            enableStreamFlag = static_cast<bool>(success);
            if (enableStreamFlag)
            {
                auto groupHandler = ptr->getGroup(streamGroupString);
                if (! (this->*loader)(groupHandler, metaData) )
                {
                    std::cerr << logPrefix << streamGroupString << " group could not be initialized from the configuration file."
                              << std ::endl;
                    return false;
                }
            }
        }

        return true;
    }

    /**
     * Configure remote control board remapper meta data
     * Related to kinematics and other joint/motor relevant quantities
     */
    bool configureRemoteControlBoardRemapper(std::weak_ptr<IParametersHandler> handler,
                                             SensorBridgeMetaData& metaData)
    {
        constexpr std::string_view logPrefix = "[YarpSensorBridge::Impl::configureRemoteControlBoardRemapper] ";
        auto ptr = handler.lock();
        if (ptr == nullptr) { return false; }

        if (!ptr->getParameter("joints_list", metaData.sensorsList.jointsList, VectorResizeMode::Resizable))
        {
            std::cerr << logPrefix << " Required parameter \"joints_list\" not available in the configuration"
                      << std::endl;
            return false;
        }

        metaData.bridgeOptions.nrJoints = metaData.sensorsList.jointsList.size();
        return true;
    }

    /**
     * Configure inertial sensors meta data
     */
    bool configureInertialSensors(std::weak_ptr<IParametersHandler> handler,
                                  SensorBridgeMetaData& metaData)
    {
        constexpr std::string_view logPrefix = "[YarpSensorBridge::Impl::configureInertialSensors] ";
        auto ptr = handler.lock();
        if (ptr == nullptr) { return false; }

        if (ptr->getParameter("imu_list", metaData.sensorsList.IMUsList, VectorResizeMode::Resizable))
        {
            metaData.bridgeOptions.isIMUEnabled = true;
        }

        if (ptr->getParameter("accelerometer_list", metaData.sensorsList.linearAccelerometersList, VectorResizeMode::Resizable))
        {
            metaData.bridgeOptions.isLinearAccelerometerEnabled = true;
        }

        if (ptr->getParameter("gyroscopes_list", metaData.sensorsList.gyroscopesList, VectorResizeMode::Resizable))
        {
            metaData.bridgeOptions.isGyroscopeEnabled = true;
        }

        if (ptr->getParameter("orientation_sensors_list", metaData.sensorsList.orientationSensorsList, VectorResizeMode::Resizable))
        {
            metaData.bridgeOptions.isOrientationSensorEnabled = true;
        }

        if (ptr->getParameter("magnetometers_list", metaData.sensorsList.magnetometersList, VectorResizeMode::Resizable))
        {
            metaData.bridgeOptions.isMagnetometerEnabled = true;
        }

        return true;
    }

    /**
     * Configure six axis force torque sensors meta data
     */
    bool configureSixAxisForceTorqueSensors(std::weak_ptr<IParametersHandler> handler,
                                            SensorBridgeMetaData& metaData)
    {
        constexpr std::string_view logPrefix = "[YarpSensorBridge::Impl::configureSixAxisForceTorqueSensors] ";
        auto ptr = handler.lock();
        if (ptr == nullptr) { return false; }

        if (!ptr->getParameter("sixaxis_forcetorque_sensors_list", metaData.sensorsList.sixAxisForceTorqueSensorsList, VectorResizeMode::Resizable))
        {
            std::cerr << logPrefix << " Required parameter \"sixaxis_forcetorque_sensors_list\" not available in the configuration"
                      << std::endl;
            return false;
        }

        return true;
    }

    /**
     * Configure cartesian wrenches meta data
     */
    bool configureCartesianWrenches(std::weak_ptr<IParametersHandler> handler,
                                    SensorBridgeMetaData& metaData)
    {
        constexpr std::string_view logPrefix = "[YarpSensorBridge::Impl::configureCartesianWrenchSensors] ";
        auto ptr = handler.lock();
        if (ptr == nullptr) { return false; }

        if (!ptr->getParameter("cartesian_wrenches_list", metaData.sensorsList.cartesianWrenchesList, VectorResizeMode::Resizable))
        {
            std::cerr << logPrefix << " Required parameter \"cartesian_wrenches_list\" not available in the configuration"
                      << std::endl;
            return false;
        }

        return true;
    }

    /**
     * Configure cameras meta data
     */
    bool configureCameras(std::weak_ptr<IParametersHandler> handler,
                          SensorBridgeMetaData& metaData)
    {
        constexpr std::string_view logPrefix = "[YarpSensorBridge::Impl::configureCameras] ";
        auto ptr = handler.lock();
        if (ptr == nullptr) { return false; }

        if (ptr->getParameter("rgb_cameras_list", metaData.sensorsList.rgbCamerasList, VectorResizeMode::Resizable))
        {
            metaData.bridgeOptions.isCameraEnabled = true;

            std::vector<int> rgbWidth, rgbHeight;
            if (ptr->getParameter("rgb_image_width", rgbWidth, VectorResizeMode::Resizable))
            {
                std::cerr << logPrefix << " Required parameter \"rgb_image_width\" not available in the configuration"
                      << std::endl;
                return false;
            }

            if (ptr->getParameter("rgb_image_height", rgbHeight, VectorResizeMode::Resizable))
            {
                std::cerr << logPrefix << " Required parameter \"rgb_image_height\" not available in the configuration"
                      << std::endl;
                return false;
            }

            if ( (rgbWidth.size() != metaData.sensorsList.rgbCamerasList.size()) ||
                (rgbHeight.size() != metaData.sensorsList.rgbCamerasList.size()) )
            {
                std::cerr << logPrefix << " Parameters list size mismatch" << std::endl;
                return false;
            }

            for (int idx = 0; idx < rgbHeight.size(); idx++)
            {
                std::pair<int, int> imgDimensions(rgbWidth[idx], rgbHeight[idx]);
                auto cameraName{metaData.sensorsList.rgbCamerasList[idx]};
                metaData.bridgeOptions.imgDimensions[cameraName] = imgDimensions;
            }
        }

        if (ptr->getParameter("depth_cameras_list", metaData.sensorsList.depthCamerasList, VectorResizeMode::Resizable))
        {
            metaData.bridgeOptions.isCameraEnabled = true;

            std::vector<int> depthCamWidth, depthCamHeight;
            if (ptr->getParameter("depth_image_width", depthCamWidth, VectorResizeMode::Resizable))
            {
                std::cerr << logPrefix << " Required parameter \"depth_image_width\" not available in the configuration"
                          << std::endl;
                return false;
            }

            if (ptr->getParameter("depth_image_height", depthCamHeight, VectorResizeMode::Resizable))
            {
                std::cerr << logPrefix << " Required parameter \"depth_image_height\" not available in the configuration"
                          << std::endl;
                return false;
            }

            if ( (depthCamWidth.size() != metaData.sensorsList.depthCamerasList.size()) ||
                (depthCamHeight.size() != metaData.sensorsList.depthCamerasList.size()) )
            {
                std::cerr << logPrefix << " Parameters list size mismatch" << std::endl;
                return false;
            }

            for (int idx = 0; idx < depthCamHeight.size(); idx++)
            {
                std::pair<int, int> imgDimensions(depthCamWidth[idx], depthCamHeight[idx]);
                auto cameraName{metaData.sensorsList.depthCamerasList[idx]};
                metaData.bridgeOptions.imgDimensions[cameraName] = imgDimensions;
            }
        }

        return true;
    }

    /**
     * Attach device with IGenericSensor or IAnalogSensor interfaces
     * Important assumptions here,
     * - Any generic sensor with 12 channels is a IMU sensor
     * - Any generic sensor with 6 channels is a cartesian wrench sensor
     * - Any analog sensor with 6 channels is a six axis force torque sensor
     */
    template <typename SensorType>
    bool attachGenericOrAnalogSensor(const yarp::dev::PolyDriverList& devList,
                                     const std::string& sensorName,
                                     const int& nrChannelsInSensor,
                                     std::unordered_map<std::string, SensorType*>& sensorMap)
    {
        constexpr std::string_view logPrefix = "[YarpSensorBridge::Impl::attachGenericOrAnalogSensor] ";
        for (int devIdx = 0; devIdx < devList.size(); devIdx++)
        {
            if (sensorName != devList[devIdx]->key)
            {
                continue;
            }

            SensorType* sensorInterface{nullptr};
            if (devList[devIdx]->poly->view(sensorInterface))
            {
                if (sensorInterface == nullptr)
                {
                    std::cerr << logPrefix << sensorName << " Could not view interface." << std::endl;
                    return false;
                }

                int nrChannels;
                if constexpr (std::is_same_v<SensorType, yarp::dev::IAnalogSensor>)
                {
                    nrChannels = sensorInterface->getChannels();
                }
                else if constexpr (std::is_same_v<SensorType, yarp::dev::IGenericSensor>)
                {
                    sensorInterface->getChannels(&nrChannels);
                }

                if (nrChannels != nrChannelsInSensor)
                {
                    std::cerr << logPrefix << sensorName << " Mismatch in the expected number of channels in the sensor stream." << std::endl;
                    return false;
                }

                sensorMap[devList[devIdx]->key] = sensorInterface;
            }
            else
            {
                std::cerr << logPrefix << sensorName << " Could not view interface." << std::endl;
                return false;
            }
        }
        return true;
    }

    template <typename SensorType>
    bool attachAllGenericOrAnalogSensors(const yarp::dev::PolyDriverList& devList,
                                         std::unordered_map<std::string, SensorType*>& sensorMap,
                                         const int& nrChannelsInSensor,
                                         const std::vector<std::string>& sensorList,
                                         std::string_view interfaceType)
    {
        constexpr std::string_view logPrefix = "[YarpSensorBridge::Impl::attachAllGenericOrAnalogSensors] ";

        bool allSensorsAttachedSuccesful{true};
        for (auto genericSensorCartesianWrench : sensorList)
        {
            if (!attachGenericOrAnalogSensor(devList,
                                             genericSensorCartesianWrench,
                                             nrChannelsInSensor,
                                             sensorMap))
            {
                allSensorsAttachedSuccesful = false;
                break;
            }
        }

        if (!allSensorsAttachedSuccesful ||
            (sensorMap.size() != sensorList.size()) )
        {
            std::cerr << logPrefix << " Could not attach all desired " << interfaceType << " ." << std::endl;
            return false;
        }

        return true;
    }

    /**
     * Attach Remapped Multiple Analog Sensor Interfaces and check available sensors
     */
    template <typename MASSensorType>
    bool attachAndCheckMASSensors(const yarp::dev::PolyDriverList& devList,
                                  MASSensorType* sensorInterface,
                                  const std::vector<std::string>& sensorList,
                                  const std::string_view interfaceName)
    {
        constexpr std::string_view logPrefix = "[YarpSensorBridge::Impl::attachAndCheckMASSensors] ";
        if (!attachRemappedMASSensor(devList, sensorInterface))
        {
            std::cerr << logPrefix << " Could not find " << interfaceName << " interface." << std::endl;
            return false;
        }

        if (!checkAttachedMASSensors(devList, sensorInterface, sensorList))
        {
            std::cerr << logPrefix << " Could not find atleast one of the required sensors." << std::endl;
            return false;
        }
        return true;
    }

    /**
     * Attach Remapped Multiple Analog Sensor Interfaces
     * Looks for a specific MAS Sensor interface in the attached MAS Remapper
     */
    template <typename MASSensorType>
    bool attachRemappedMASSensor(const yarp::dev::PolyDriverList& devList,
                                 MASSensorType* masSensorInterface)
    {
        constexpr std::string_view logPrefix = "[YarpSensorBridge::Impl::attachRemappedMASSensor] ";
        bool broken{false};
        for (int devIdx = 0; devIdx < devList.size(); devIdx++)
        {
            MASSensorType* sensorInterface{nullptr};
            devList[devIdx]->poly->view(sensorInterface);
            if (sensorInterface == nullptr)
            {
                continue;
            }

            // break out of the loop if we find relevant MAS interface
            masSensorInterface = sensorInterface;
            broken = true;
            break;
        }

        if (!broken || masSensorInterface == nullptr)
        {
            std::cerr << logPrefix << " Could not view interface." << std::endl;
            return false;
        }
        return true;
    }

    /**
     * Check if all the desired MAS sensors are availabled in the attached MAS interface
     */
    template <typename MASSensorType>
    bool checkAttachedMASSensors(const yarp::dev::PolyDriverList& devList,
                                 MASSensorType* sensorInterface,
                                 const std::vector<std::string>& sensorList)
    {
        constexpr std::string_view logPrefix = "[YarpSensorBridge::Impl::checkAttachedMASSensors] ";

        auto nrSensors{getNumberOfMASSensors(sensorInterface)};
        if (nrSensors != sensorList.size())
        {
            std::cerr << logPrefix << "Expecting the same number of MAS sensors attached to the Bridge as many mentioned in the initialization step" << std::endl;
            return false;
        }

        for (const auto& masSensorName : sensorList)
        {
            bool sensorFound{false};
            for (size_t attachedIdx = 0; attachedIdx < nrSensors; attachedIdx++)
            {
                std::string attachedSensorName;
                if (!getMASSensorName(sensorInterface, attachedIdx, attachedSensorName))
                {
                    continue;
                }

                if (attachedSensorName == masSensorName)
                {
                    sensorFound = true;
                    break;
                }
            }

            if (!sensorFound)
            {
                 std::cerr << logPrefix << "Bridge could not attach to MAS Sensor " << masSensorName << "." << std::endl;
            }
        }

        return true;
    }

    /**
     *  Get number of MAS Sensors
     */
    template <typename MASSensorType>
    size_t getNumberOfMASSensors(MASSensorType* sensorInterface)
    {
        if constexpr (std::is_same_v<MASSensorType, yarp::dev::IThreeAxisGyroscopes>)
        {
            return sensorInterface->getNrOfThreeAxisGyroscopes();
        }
        else if constexpr (std::is_same_v<MASSensorType, yarp::dev::IThreeAxisLinearAccelerometers>)
        {
            return sensorInterface->getNrOfThreeAxisLinearAccelerometers();
        }
        else if constexpr (std::is_same_v<MASSensorType, yarp::dev::IThreeAxisMagnetometers>)
        {
            return sensorInterface->getNrOfThreeAxisMagnetometers();
        }
        else if constexpr (std::is_same_v<MASSensorType, yarp::dev::IOrientationSensors>)
        {
            return sensorInterface->getNrOfOrientationSensors();
        }
        else if constexpr (std::is_same_v<MASSensorType, yarp::dev::ISixAxisForceTorqueSensors>)
        {
            return sensorInterface->getNrOfSixAxisForceTorqueSensors();
        }

        return 0;
    }

    /**
     *  Get name of MAS Sensors
     */
    template <typename MASSensorType>
    bool getMASSensorName(MASSensorType* sensorInterface,
                          const size_t& sensIdx,
                          std::string& sensorName)
    {
        if constexpr (std::is_same_v<MASSensorType, yarp::dev::IThreeAxisGyroscopes>)
        {
            return sensorInterface->getThreeAxisGyroscopeName(sensIdx, sensorName);
        }
        else if constexpr (std::is_same_v<MASSensorType, yarp::dev::IThreeAxisLinearAccelerometers>)
        {
            return sensorInterface->getThreeAxisLinearAccelerometerName(sensIdx, sensorName);
        }
        else if constexpr (std::is_same_v<MASSensorType, yarp::dev::IThreeAxisMagnetometers>)
        {
            return sensorInterface->getThreeAxisMagnetometerName(sensIdx, sensorName);
        }
        else if constexpr (std::is_same_v<MASSensorType, yarp::dev::IOrientationSensors>)
        {
            return sensorInterface->getOrientationSensorName(sensIdx, sensorName);
        }
        else if constexpr (std::is_same_v<MASSensorType, yarp::dev::ISixAxisForceTorqueSensors>)
        {
            return sensorInterface->getSixAxisForceTorqueSensorName(sensIdx, sensorName);
        }
        return true;
    }

    /**
     * Get all sensor names in a MAS Inerface
     */
    template <typename MASSensorType>
    std::vector<std::string> getAllSensorsInMASInterface(MASSensorType* sensorInterface)
    {
        std::vector<std::string> availableSensorNames;
        if constexpr (std::is_same_v<MASSensorType, yarp::dev::IThreeAxisGyroscopes>)
        {
            auto nrSensors = sensorInterface->getNrOfThreeAxisGyroscopes();
            for (size_t sensIdx = 0; sensIdx < nrSensors; sensIdx++)
            {
                std::string sensName;
                sensorInterface->getThreeAxisGyroscopeName(sensIdx, sensName);
                availableSensorNames.push_back(sensName);
            }
        }
        else if constexpr (std::is_same_v<MASSensorType, yarp::dev::IThreeAxisLinearAccelerometers>)
        {
            auto nrSensors = sensorInterface->getNrOfThreeAxisLinearAccelerometers();
            for (size_t sensIdx = 0; sensIdx < nrSensors; sensIdx++)
            {
                std::string sensName;
                sensorInterface->getThreeAxisLinearAccelerometerName(sensIdx, sensName);
                availableSensorNames.push_back(sensName);
            }
        }
        else if constexpr (std::is_same_v<MASSensorType, yarp::dev::IThreeAxisMagnetometers>)
        {
            auto nrSensors = sensorInterface->getNrOfThreeAxisMagnetometers();
            for (size_t sensIdx = 0; sensIdx < nrSensors; sensIdx++)
            {
                std::string sensName;
                sensorInterface->getThreeAxisMagnetometerName(sensIdx, sensName);
                availableSensorNames.push_back(sensName);
            }
        }
        else if constexpr (std::is_same_v<MASSensorType, yarp::dev::IOrientationSensors>)
        {
            auto nrSensors = sensorInterface->getNrOfOrientationSensors();
            for (size_t sensIdx = 0; sensIdx < nrSensors; sensIdx++)
            {
                std::string sensName;
                sensorInterface->getOrientationSensorName(sensIdx, sensName);
                availableSensorNames.push_back(sensName);
            }
        }
        else if constexpr (std::is_same_v<MASSensorType, yarp::dev::ISixAxisForceTorqueSensors>)
        {
            auto nrSensors = sensorInterface->getNrOfSixAxisForceTorqueSensors();
            for (size_t sensIdx = 0; sensIdx < nrSensors; sensIdx++)
            {
                std::string sensName;
                sensorInterface->getSixAxisForceTorqueSensorName(sensIdx, sensName);
                availableSensorNames.push_back(sensName);
            }
        }
        return availableSensorNames;
    }

    /**
     * Attach cameras
     */
    template <typename CameraType>
    bool attachCamera(const yarp::dev::PolyDriverList& devList,
                      const std::string sensorName,
                      std::unordered_map<std::string, CameraType* >& sensorMap)
    {
        constexpr std::string_view logPrefix = "[YarpSensorBridge::Impl::attachCamera] ";
        for (int devIdx = 0; devIdx < devList.size(); devIdx++)
        {
            if (sensorName != devList[devIdx]->key)
            {
                continue;
            }

            CameraType* cameraInterface{nullptr};
            if (devList[devIdx]->poly->view(cameraInterface))
            {
                if (cameraInterface == nullptr)
                {
                    std::cerr << logPrefix << " Could not view interface." << std::endl;
                    return false;
                }
                sensorMap[devList[devIdx]->key] = cameraInterface;
            }
        }
        return true;
    }

    /**
     * Check if sensor is available in the relevant sensor map
     */
    template <typename SensorType>
    bool checkSensor(const std::unordered_map<std::string, SensorType* >& sensorMap,
                     const std::string& sensorName)
    {
        if (sensorMap.find(sensorName) == sensorMap.end())
        {
            return false;
        }

        return true;
    }

    /**
     * Check if the bridge is successfully initialized and attached to required device drivers
     */
    bool checkValid(const std::string_view methodName)
    {
        if (!(bridgeInitialized && driversAttached))
        {
            std::cerr << methodName << " SensorBridge is not ready. Please initialize and set drivers list.";
            return false;
        }
        return true;
    }

    /**
     *  Attach generic IMU sensor types and MAS inertials
     */
    bool attachAllInertials(const yarp::dev::PolyDriverList& devList)
    {
        constexpr std::string_view logPrefix = "[YarpSensorBridge::Impl::attachAllInertials] ";
        if (metaData.bridgeOptions.isIMUEnabled)
        {
            // check if a generic sensor has 12 channels implying it is a IMU sensor through a GenericSensor Interfaces
            std::string_view interfaceType{"Generic IMU Interface"};
            if (!attachAllGenericOrAnalogSensors(devList,
                                                 wholeBodyAnalogIMUInterface,
                                                 nrChannelsInYARPGenericIMUSensor,
                                                 metaData.sensorsList.IMUsList,
                                                 interfaceType))
            {
                return false;
            }
        }

        if (metaData.bridgeOptions.isLinearAccelerometerEnabled)
        {
            std::string_view interfaceType{"IThreeAxisLinearAccelerometers"};
            if (!attachAndCheckMASSensors(devList, wholeBodyMASInertialsInterface.accelerometers, metaData.sensorsList.linearAccelerometersList, interfaceType))
            {
                return false;
            }
        }

        if (metaData.bridgeOptions.isGyroscopeEnabled)
        {
            std::string_view interfaceType{"IThreeAxisGyroscopes"};
            if (!attachAndCheckMASSensors(devList, wholeBodyMASInertialsInterface.gyroscopes, metaData.sensorsList.gyroscopesList, interfaceType))
            {
                return false;
            }
        }

        if (metaData.bridgeOptions.isOrientationSensorEnabled)
        {
            std::string_view interfaceType{"IOrientationSensors"};
            if (!attachAndCheckMASSensors(devList, wholeBodyMASInertialsInterface.orientationSensors, metaData.sensorsList.orientationSensorsList, interfaceType))
            {
                return false;
            }
        }

        if (metaData.bridgeOptions.isMagnetometerEnabled)
        {
            std::string_view interfaceType{"IThreeAxisMagnetometers"};
            if (!attachAndCheckMASSensors(devList, wholeBodyMASInertialsInterface.magnetometers, metaData.sensorsList.magnetometersList, interfaceType))
            {
                return false;
            }
        }
        return true;
    }

    /**
     * Attach a remapped control board and check the availability of desired interface
     * Further, resize joint data buffers and check if
     * the control board joints list and the desired joints list match
     * Also, maintain a remapping index buffer for adapting to arbitrary joint list serializations
     */
    bool attachRemappedRemoteControlBoard(const yarp::dev::PolyDriverList& devList)
    {
        if (!metaData.bridgeOptions.isKinematicsEnabled)
        {
            // do nothing
            return true;
        }

        constexpr std::string_view logPrefix = "[YarpSensorBridge::Impl::attachRemappedRemoteControlBoard] ";
        // expects only one remotecontrolboard device attached to it, if found break!
        // if there multiple remote control boards, then  use a remapper to create a single remotecontrolboard
        bool ok{true};
        for (int devIdx = 0; devIdx < devList.size(); devIdx++)
        {
            ok = ok && devList[devIdx]->poly->view(controlBoardRemapperInterfaces.axis);
            ok = ok && devList[devIdx]->poly->view(controlBoardRemapperInterfaces.encoders);
            if (ok)
            {
                break;
            }
        }

        if (!ok)
        {
            std::cerr << logPrefix << " Could not find a remapped remote control board with the desired interfaces" << std::endl;
            return false;
        }

        resetControlBoardBuffers();
        if (!compareControlBoardJointsList())
        {
            std::cerr << logPrefix << " Could not attach to remapped control board interface" << std::endl;
            return false;
        }

        return true;
    }

    /**
     * resize and set control board buffers to zero
     */
    void resetControlBoardBuffers()
    {
        // firstly resize all the controlboard data buffers
        controlBoardRemapperMeasures.remappedJointIndices.resize(metaData.bridgeOptions.nrJoints);
        controlBoardRemapperMeasures.jointPositions.resize(metaData.bridgeOptions.nrJoints);
        controlBoardRemapperMeasures.jointVelocities.resize(metaData.bridgeOptions.nrJoints);

        // zero buffers
        controlBoardRemapperMeasures.remappedJointIndices.setZero();
        controlBoardRemapperMeasures.jointPositions.setZero();
        controlBoardRemapperMeasures.jointVelocities.setZero();
    }

    /**
     * check and match control board joints with the sensorbridge joints list
     */
    bool compareControlBoardJointsList()
    {
        constexpr std::string_view logPrefix = "[YarpSensorBridge::Impl::compareControlBoardJointsList] ";
        // get the names of all the joints available in the attached remote control board remapper
        std::vector<std::string> controlBoardJoints;
        int controlBoardDOFs;
        controlBoardRemapperInterfaces.encoders->getAxes(&controlBoardDOFs);
        for (int DOF = 0; DOF < controlBoardDOFs; DOF++)
        {
            std::string joint;
            controlBoardRemapperInterfaces.axis->getAxisName(DOF, joint);
            controlBoardJoints.push_back(joint);
        }

        // check if the joints in the desired joint list are available in the controlboard joints list
        // if available get the control board index at which the desired joint is available
        // this is required in order to remap the control board joints on to the desired joints
        // this needs to be optimized with better STL algorithms
        bool jointFound{false};
        for (int desiredDOF = 0; desiredDOF < metaData.sensorsList.jointsList.size(); desiredDOF++)
        {
            jointFound = false;
            for (int DOF = 0; DOF < controlBoardDOFs; DOF++)
            {
                if (metaData.sensorsList.jointsList[desiredDOF] == controlBoardJoints[DOF])
                {
                    controlBoardRemapperMeasures.remappedJointIndices[desiredDOF] = DOF;
                    jointFound = true;
                    break;
                }
            }

            if (!jointFound)
            {
                std::cerr << logPrefix << " Could not find a desired joint from the configuration in the attached control board remapper." << std::endl;
                return false;
            }
        }

        if (!jointFound)
        {
            return false;
        }

        std::cout << logPrefix << "Found all joints with the remapped index " << std::endl;
        for (int idx =0; idx < controlBoardRemapperMeasures.remappedJointIndices.size(); idx++)
        {
            std::cout << logPrefix << "Remapped Index: " << controlBoardRemapperMeasures.remappedJointIndices[idx]
                                   << " , Joint name:  " << controlBoardJoints[controlBoardRemapperMeasures.remappedJointIndices[idx]]
                                   << std::endl;
        }
        return true;
    }

    bool attachAllSixAxisForceTorqueSensors(const yarp::dev::PolyDriverList& devList)
    {
        if (!metaData.bridgeOptions.isSixAxisForceTorqueSensorEnabled)
        {
            // do nothing
            return true;
        }

        std::vector<std::string> analogFTSensors;
        // attach MAS sensors
        if (attachRemappedMASSensor(devList,
                                    wholeBodyMASForceTorquesInterface.sixAxisFTSensors))
        {
            // get MAS FT sensor names
            auto MASFTs = getAllSensorsInMASInterface(wholeBodyMASForceTorquesInterface.sixAxisFTSensors);
            auto copySensorsList = metaData.sensorsList.sixAxisForceTorqueSensorsList;

            // compare with sensorList - those not available in the MAS interface list
            // are assumed to be analog FT sensors
            std::sort(MASFTs.begin(), MASFTs.end());
            std::sort(copySensorsList.begin(), copySensorsList.end());
            std::set_intersection(MASFTs.begin(), MASFTs.end(),
                                  copySensorsList.begin(), copySensorsList.end(),
                                  std::back_inserter(analogFTSensors));
        }
        else
        {
            // if there are no MAS FT sensors then all the FT sensors in the configuration
            // are analog FT sensors
            analogFTSensors = metaData.sensorsList.sixAxisForceTorqueSensorsList;
        }

        // check if a generic sensor has 12 channels implying it is a IMU sensor through a GenericSensor Interfaces
        std::string_view interfaceType{"Analog Six Axis FT Interface"};
        if (!attachAllGenericOrAnalogSensors(devList,
                                             wholeBodyAnalogSixAxisFTSensorsInterface,
                                             nrChannelsInYARPAnalogSixAxisFTSensor,
                                             analogFTSensors,
                                             interfaceType))
        {
            return false;
        }

        return true;
    }

    /**
     * Attach to cartesian wrench interface
     */
    bool attachCartesianWrenchInterface(const yarp::dev::PolyDriverList& devList)
    {
        if (!metaData.bridgeOptions.isCartesianWrenchEnabled)
        {
            // do nothing
            return true;
        }

        if (metaData.bridgeOptions.isCartesianWrenchEnabled)
        {
            // check if a generic sensor has 6 channels implying it is a cartesian wrench sensor through a GenericSensor Interfaces
            std::string_view interfaceType{"Cartesian Wrench Interface"};
            if (!attachAllGenericOrAnalogSensors(devList,
                                                 wholeBodyCartesianWrenchInterface,
                                                 nrChannelsInYARPGenericCartesianWrench,
                                                 metaData.sensorsList.cartesianWrenchesList,
                                                 interfaceType))
            {
                return false;
            }

        }
        return true;
    }

    /**
     * Attach all cameras
     */
    bool attachAllCameras(const yarp::dev::PolyDriverList& devList)
    {
        if (!metaData.bridgeOptions.isCameraEnabled)
        {
            // do nothing
            return true;
        }

        std::string_view interfaceType{"RGB Cameras"};
        if (!attachAllCamerasOfSpecificType(devList,
                                            metaData.sensorsList.rgbCamerasList,
                                            metaData.bridgeOptions.imgDimensions,
                                            interfaceType,
                                            wholeBodyFrameGrabberInterface,
                                            wholeBodyCameraImages))
        {
            return false;
        }

        std::string_view interfaceTypeDepth = "Depth Cameras";
        if (!attachAllCamerasOfSpecificType(devList,
                                            metaData.sensorsList.depthCamerasList,
                                            metaData.bridgeOptions.imgDimensions,
                                            interfaceTypeDepth,
                                            wholeBodyRGBDInterface,
                                            wholeBodyCameraImages))
        {
            return false;
        }

        return true;
    }

    /**
     * Attach all cameras of specific type and resize image buffers
     */
    template <typename CameraType>
    bool attachAllCamerasOfSpecificType(const yarp::dev::PolyDriverList& devList,
                                        const std::vector<std::string>& camList,
                                        const std::unordered_map<std::string, std::pair<int, int> >& imgDimensionsMap,
                                        std::string_view interfaceType,
                                        std::unordered_map<std::string, CameraType* >& sensorMap,
                                        std::unordered_map<std::string, Eigen::MatrixXd>& imgBuffersMap)
    {
        constexpr std::string_view logPrefix = "[YarpSensorBridge::Impl::attachAllCamerasOfSpecificType] ";
        for (auto cam : camList)
        {
            if (!attachCamera(devList, cam, sensorMap))
            {
                return false;
            }
        }

        if (sensorMap.size() != camList.size())
        {
            std::cout << logPrefix << " could not attach all desired rgb cameras " << interfaceType  << "." << std::endl;
            return false;
        }

        if (!resizeImageBuffers(camList,
                                imgDimensionsMap,
                                imgBuffersMap))
        {
            std::cout << logPrefix << " Failed to resize camera buffers of type " << interfaceType  << "." << std::endl;
            return false;
        }
        return true;
    }

    /**
     * Resize image buffers
     */
    bool resizeImageBuffers(const std::vector<std::string>& camList,
                            const std::unordered_map<std::string, std::pair<int, int> >& imgDimensionsMap,
                            std::unordered_map<std::string, Eigen::MatrixXd>& imgBuffersMap)
    {
        for (const auto& cam : camList)
        {
            auto iter = imgDimensionsMap.find(cam);
            if (iter == imgDimensionsMap.end())
            {
                return false;
            }

            auto imgDim = iter->second;
            imgBuffersMap[cam].resize(imgDim.first, imgDim.second);
        }
        return true;
    }
};

}
}
#endif // BIPEDAL_LOCOMOTION_ROBOT_INTERFACE_YARP_SENSOR_BRIDGE_IMPL_H



// LoggerManager.h
#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <unordered_map>
#include <mutex>
#include <filesystem>
#include <memory>
#include <iostream>

// 头文件式实现（全部内联成员函数）
class LoggerManager {
private:
	// 单例模式
	LoggerManager() {
		initLogDirectory();
	}
	~LoggerManager() = default;

	// 禁止拷贝和移动
	LoggerManager(const LoggerManager&) = delete;
	LoggerManager& operator=(const LoggerManager&) = delete;
	LoggerManager(LoggerManager&&) = delete;
	LoggerManager& operator=(LoggerManager&&) = delete;

	static void initLogDirectory() {
		try {
			std::filesystem::path logDir("logs");
			if (!std::filesystem::exists(logDir)) {
				std::filesystem::create_directories(logDir);
			}
		} catch (const std::exception& ex) {
			std::cerr << "Failed to create logs directory: " << ex.what() << std::endl;
		}
	}

	// 静态日志映射表与互斥锁
	inline static std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> loggerMap;
	inline static std::mutex loggerMutex;

public:
	static LoggerManager& getInstance() {
		static LoggerManager instance; // 懒汉式单例（使用时创建实例）
		return instance;
	}

	static std::shared_ptr<spdlog::logger> getLogger(const std::string& loggerName) {
		getInstance();
	
		// 首先检查缓存
		{
			std::lock_guard<std::mutex> lock(loggerMutex);
			auto it = loggerMap.find(loggerName);
			if (it != loggerMap.end()) {
				return it->second; // 如果存在，直接返回
			}
		}
	
		try {
			// 创建sink集合
			std::vector<spdlog::sink_ptr> sinks;
			
			// 1. 添加文件sink
			auto rotatingSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
				"logs/" + loggerName + ".log", 1048576 * 5, 3);
			rotatingSink->set_level(spdlog::level::info);
			rotatingSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] %v");
			sinks.push_back(rotatingSink);
			
			// 2. 添加控制台sink
			auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			consoleSink->set_level(spdlog::level::debug); // 控制台可以设置为更详细的级别
			consoleSink->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v"); // 简洁的控制台输出格式
			sinks.push_back(consoleSink);
			
			// 创建多sink logger
			auto logger = std::make_shared<spdlog::logger>(loggerName, sinks.begin(), sinks.end());
			logger->set_level(spdlog::level::debug); // 设置为最低级别让每个sink自己过滤
			logger->flush_on(spdlog::level::info); // 确保info及以上级别的消息立即刷新
	
			{
				std::lock_guard<std::mutex> lock(loggerMutex);
				// Double-check if the logger was created by another thread
				auto it = loggerMap.find(loggerName);
				if (it != loggerMap.end()) {
					return it->second;
				}
				loggerMap[loggerName] = logger;
			}
			return logger;
		} catch (const std::exception& e) {
			std::cerr << "Logger creation failed: " << e.what() << std::endl;
			return spdlog::default_logger(); // 返回默认 logger
		}
	}

	static void setGlobalLevel(spdlog::level::level_enum logLevel) {
		std::lock_guard<std::mutex> lock(loggerMutex);
		for (auto& pair : loggerMap) {
			pair.second->set_level(logLevel);
			for (auto& sink : pair.second->sinks()) {
				sink->set_level(logLevel);
			}
		}
	}

	static void setGlobalLevel(spdlog::level::level_enum loggerLevel, spdlog::level::level_enum sinkLevel) {
		std::lock_guard<std::mutex> lock(loggerMutex);
		for (auto& pair : loggerMap) {
			auto& logger = pair.second;
			// 设置 logger 级别
			logger->set_level(loggerLevel);
			
			// 设置所有 sink 的级别
			for (auto& sink : logger->sinks()) {
				sink->set_level(sinkLevel);
			}
		}
	}
};
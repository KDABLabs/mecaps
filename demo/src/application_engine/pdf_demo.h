#pragma once

#include "app_window.h"
#include "pdf_controller.h"
#include "pdf_viewport.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

class PdfDemo
{
  public:
	PdfDemo(const PdfSingleton &ui, std::unique_ptr<mecaps::pdf::Backend> backend);
	~PdfDemo();

	PdfDemo(const PdfDemo &) = delete;
	PdfDemo &operator=(const PdfDemo &) = delete;

  private:
	void open(const slint::SharedString &path);
	void runFileWorker();
	void completeFileRead(
	        mecaps::pdf::GenerationId generation,
	        std::vector<std::byte> data,
	        std::string_view error);
	void showPage(std::size_t pageIndex);
	void viewportChanged(double width, double height, double displayScale);
	void setFitMode(mecaps::pdf::FitMode mode);
	void zoomBy(double factor, double anchorX, double anchorY);
	void panBy(double deltaX, double deltaY, bool immediate);
	void scheduleRender(bool immediate = false);
	void renderViewport(mecaps::pdf::GenerationId generation);
	void updatePreview();
	void publishError(mecaps::pdf::DocumentError error);

	struct FileRequest {
		mecaps::pdf::GenerationId generation;
		std::string path;
	};

	struct PublicationState {
		std::atomic_bool alive { true };
		std::atomic<mecaps::pdf::GenerationId> generation { 0 };
	};

	const PdfSingleton &m_ui;
	std::unique_ptr<mecaps::pdf::Controller> m_controller;
	mecaps::pdf::Viewport m_viewport;
	slint::Timer m_renderTimer;
	std::shared_ptr<PublicationState> m_publication = std::make_shared<PublicationState>();
	std::mutex m_fileMutex;
	std::condition_variable m_fileReady;
	std::optional<FileRequest> m_fileRequest;
	std::thread m_fileWorker;
	mecaps::pdf::GenerationId m_generation { 0 };
	std::size_t m_pageIndex { 0 };
	std::size_t m_pageCount { 0 };
	std::optional<mecaps::pdf::PageClip> m_renderedClip;
	bool m_hasPage { false };
	bool m_stopping { false };
};
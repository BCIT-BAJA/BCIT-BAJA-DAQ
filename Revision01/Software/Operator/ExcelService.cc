//

#include "pch.h"
#include "ExcelService.h"

// i think a combination is a good median solution.
// perhaps i can dump a .csv, but then run a second process that attempts to use libxlsx to graph such an outlier.
// (1khz)(60s)(60min)(2hr) = 7 million rows. say, 80 bytes, 7 million * 80 bytes ~560 megabytes of data.
// filtering must be applied, and everything should be extremely stable, with no memory allocations,
// and no user input / false behaviour.
// in addition, any crashes should just autostart the program again...
//
// checkout xlslib. .xls 
// https://github.com/JanX2/xlslib/blob/rebased-on-svn/xlslib/src/xlslib.h
// it allows repeated dumping of .xls object, unlike this other crappy lib :(((

//
// annoyingly, .xlsx is not meant for streaming.
// this means we'll have to put limits on bandwidth, or chunk data or something... IDK.
// We'll have to test writing speed. It *should* be fast enough to handle at LEAST 10khz, god 
// if this library can't do that due to ridiculous flushes...
//
// okay, this .xlsx file writer library is terrible.
// it's probably soooo much better to write a zlib compressed .bin... :d
// then just flush to .xlsx occassionally?
// to be honest, it makes sense just to use an in-memory buffer instead, then pump data to disk often?
// then just flush on certain boundaries, or after a certain time?
// todo: use itoa: https://github.com/jeaiii/itoa/tree/main/itoa

static bool FileExists(const char* szPath) {
	DWORD dwAttrib = GetFileAttributesA(szPath);

	return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
		!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

static bool GetLastModified_AsString(const char* filename, SYSTEMTIME* out_st) {
	WIN32_FILE_ATTRIBUTE_DATA fileInfo;

	// 1. Get file attributes including timestamps
	if (GetFileAttributesExA(filename, GetFileExInfoStandard, &fileInfo)) {

		// 2. Convert FILETIME to SYSTEMTIME (UTC)
		SYSTEMTIME stUTC;
		if(!Assure_True(FileTimeToSystemTime(&fileInfo.ftLastWriteTime, &stUTC))) {
			return false;
		}

		// 3. Convert UTC to Local Time (optional but usually preferred)
		if(!Assure_True(SystemTimeToTzSpecificLocalTime(NULL, &stUTC, out_st))) {
			return false;
		}

		return true;
	}

	return false;
}

intptr_t Thread_ExcelService(void* _) {
	Basic_SetThreadName("ExcelService");

#if c_config(debug)
	puts(MACRO_FunctionSignature());
	defer(puts(MACRO_FunctionSignature()));
#endif

	ExcelService* self = cast(ExcelService*)_;
	Y_QueueMM<ExcelService_MsgIn>::Consumer qi_consumer = self->qi.Consumer_Rent();
	defer(self->qi.Consumer_Return(&qi_consumer));

	const char* live_xlsx_filename = "live.xlsx";

	if(FileExists(live_xlsx_filename)) {
		SYSTEMTIME dead_st = { 0 };
		if(!GetLastModified_AsString(live_xlsx_filename, &dead_st)) {
			GetLocalTime(&dead_st);
		}

		char dead_xlsx_filename[MAX_PATH] = { 0 };
		snprintf(aarg(dead_xlsx_filename),
			"dead_%04d-%02d-%02d %02d%02d%02d.xlsx",
			dead_st.wYear,
			dead_st.wMonth,
			dead_st.wDay,
			dead_st.wHour,
			dead_st.wMinute,
			dead_st.wSecond
		);

		// hmm, what do we do with the live.xlsx data? put it somewhere? name it something? i don't know.
		Assure_True(CopyFileA(live_xlsx_filename, dead_xlsx_filename, false));
	}

	lxw_workbook_options workbook_options = {
		.constant_memory = LXW_FALSE,
		.tmpdir = ".",
		.use_zip64 = LXW_FALSE,
		.output_buffer = null,
		.output_buffer_size = null
	};

	lxw_workbook* live_xlsx = null;

	lxw_error xlsx_error = LXW_NO_ERROR;
	if(!Assure_True(live_xlsx = workbook_new_opt(live_xlsx_filename, &workbook_options))) { return __LINE__; }
	defer(
		while(!Assure_True(LXW_NO_ERROR == (xlsx_error = workbook_close(live_xlsx)))) {
			// loop until we close and write the notebook for good :D
		}
	);

	lxw_worksheet* worksheet = null;
	if(!Assure_True(worksheet = workbook_add_worksheet(live_xlsx, null))) { return __LINE__; }
	uint32_t worksheet_row_i = 0;

	#if 0
	for(uint32_t row_i = 0; row_i < 10; ++row_i) {
		Assure_True(LXW_NO_ERROR == (xlsx_error = worksheet_write_string(worksheet, row_i*2 + 0, 0, "Hello", null)));
		Assure_True(LXW_NO_ERROR == (xlsx_error = worksheet_write_number(worksheet, row_i*2 + 1, 0, 123, null)));
	}
	#endif

	ExcelService_MsgIn mi;
	while(true) {
		Y_Rx_e mi_pull = Y_Rx_e::Empty;
		self->qi_produce_event.AwaitSignalUntil(Timeout32_e::Infinite,
			[self, &qi_consumer, &mi, &mi_pull]() {
			return (Y_Rx_e::Empty != (mi_pull = qi_consumer.Pull_Rx(&mi)));
		});

		if(mi_pull == Y_Rx_e::Empty) {
			// timeout
		} else if(mi_pull == Y_Rx_e::Contention) {
			Y_Thread_Yield();
		} else if(mi_pull == Y_Rx_e::Success) {
			switch(mi.type) {
				case ExcelService_MsgIn_e::End: {
					/* note: race: other incoming messages are lost */
					goto end;
				} break;

				case ExcelService_MsgIn_e::Data: {
					auto& vec = mi.as.Data.vec;
					defer(DestructAt_NullSafe(&vec));

					for(auto& it : vec) {
						Assure_True(LXW_NO_ERROR == (xlsx_error = worksheet_write_number(worksheet, worksheet_row_i, 0, it, null)));
						++worksheet_row_i;
					}

					// check if we're out of room / timeout has passed to (re)create a new workbook ?
				} break;

				default: assure(false, "Unknown message type %d", mi.type);
			}
		}
	}
	end:;

	return 0;
}

void ExcelService_PublishData(ExcelService* _, const std::vector<uint16_t>& data) {
	Task_ZoneScoped_NoCallstack;

	// todo: make this thread local !!!!!!
	Y_QueueMM<ExcelService_MsgIn>::Producer qi_producer = _->qi.Producer_Rent();
	defer(_->qi.Producer_Return(&qi_producer));

	ExcelService_MsgIn msg;
	msg.Construct_Data(data);

	while(qi_producer.Push_Tx(&msg) != Y_Tx_e::Success) { Y_Thread_Yield(); } // note: todo: will lock up if full.
	_->qi_produce_event.Signal_One();
}


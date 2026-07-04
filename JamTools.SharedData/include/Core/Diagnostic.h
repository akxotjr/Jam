#pragma once

#include <string>
#include <vector>

namespace jam::tool
{
	enum class eDiagnosticSeverity
	{
		Info,
		Warning,
		Error
	};

	struct Diagnostic
	{
		eDiagnosticSeverity		severity;
		std::string				code;
		std::string				message;
		std::string				file;
		int						line   = 0;
		int						column = 0;
	};

	class DiagnosticBag
	{
	public:
		void							Add(Diagnostic diagnostic);
		void							Error(std::string code, std::string message);
		bool							HasError() const;
		const std::vector<Diagnostic>&	Items() const;

	private:
		std::vector<Diagnostic>			m_diagnostics;
	};
}

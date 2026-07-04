#include "Core/Diagnostic.h"

#include <algorithm>
#include <utility>

namespace jam::tool
{
	void DiagnosticBag::Add(Diagnostic diagnostic)
	{
		m_diagnostics.emplace_back(std::move(diagnostic));
	}

	void DiagnosticBag::Error(std::string code, std::string message)
	{
		Diagnostic diagnostic;
		diagnostic.severity = eDiagnosticSeverity::Error;
		diagnostic.code		= std::move(code);
		diagnostic.message  = std::move(message);
		Add(std::move(diagnostic));
	}

	bool DiagnosticBag::HasError() const
	{
		return std::ranges::any_of(m_diagnostics,
			[](const Diagnostic& diagnostic)
			{
				return diagnostic.severity == eDiagnosticSeverity::Error;
			});
	}

	const std::vector<Diagnostic>& DiagnosticBag::Items() const
	{
		return m_diagnostics;
	}
}

